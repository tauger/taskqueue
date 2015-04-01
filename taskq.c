/*
 * Copyright 2015 Grant Zhang
 *
 * taskq implementation based on PTHREAD
 * Modified from Solaris ZFS libzpool taskq implementation
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include "taskq.h"

// Global variables
taskq_t *system_taskq;
int taskq_now = 0;

// Caller has to hold tq->tq_lock
static task_t * task_alloc(taskq_t *tq, int tqflags)
{
	task_t *t;
	int rv;
	struct timespec ts;

again:	if ((t = tq->tq_freelist) != NULL && tq->tq_nalloc >= tq->tq_minalloc) {
		tq->tq_freelist = t->task_next;
	} else {
		if (tq->tq_nalloc >= tq->tq_maxalloc) {
			/*
			 * We don't want to exceed tq_maxalloc, but we can't
			 * wait for other tasks to complete (and thus free up
			 * task structures) without risking deadlock with
			 * the caller.  So, we just delay for one second
			 * to throttle the allocation rate. If we have tasks
			 * complete before one second timeout expires then
			 * taskq_ent_free will signal us and we will
			 * immediately retry the allocation.
			 */
			tq->tq_maxalloc_wait++;

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1;

			rv = pthread_cond_timedwait(&tq->tq_maxalloc_cv,
			    &tq->tq_lock, &ts);
			tq->tq_maxalloc_wait--;
			if (rv > 0)
				goto again;		/* signaled */
		}
		pthread_mutex_unlock(&tq->tq_lock);

		t = malloc(sizeof (task_t));

		pthread_mutex_lock(&tq->tq_lock);
		if (t != NULL)
			tq->tq_nalloc++;
	}
	return (t);
}

static void task_free(taskq_t *tq, task_t *t)
{
	if (tq->tq_nalloc <= tq->tq_minalloc) {
		t->task_next = tq->tq_freelist;
		tq->tq_freelist = t;
	} else {
		tq->tq_nalloc--;
		pthread_mutex_unlock(&tq->tq_lock);
		free(t);
		pthread_mutex_lock(&tq->tq_lock);
	}

	if (tq->tq_maxalloc_wait)
		pthread_cond_signal(&tq->tq_maxalloc_cv);
}

taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, unsigned int tqflags)
{
	task_t *t;

	if (taskq_now) {
		func(arg);
		return (1);
	}

	pthread_mutex_lock(&tq->tq_lock);
	assert(tq->tq_flags & TASKQ_ACTIVE);
	if ((t = task_alloc(tq, tqflags)) == NULL) {
		pthread_mutex_unlock(&tq->tq_lock);
		return (0);
	}
	if (tqflags & TQ_FRONT) {
		t->task_next = tq->tq_task.task_next;
		t->task_prev = &tq->tq_task;
	} else {
		t->task_next = &tq->tq_task;
		t->task_prev = tq->tq_task.task_prev;
	}
	t->task_next->task_prev = t;
	t->task_prev->task_next = t;
	t->task_func = func;
	t->task_arg = arg;
	pthread_cond_signal(&tq->tq_dispatch_cv);
	pthread_mutex_unlock(&tq->tq_lock);
	return (1);
}

/* waiting for pending task to complete */
void taskq_wait(taskq_t *tq)
{
	pthread_mutex_lock(&tq->tq_lock);
	while (tq->tq_task.task_next != &tq->tq_task || tq->tq_active != 0)
		pthread_cond_wait(&tq->tq_wait_cv, &tq->tq_lock);
	pthread_mutex_unlock(&tq->tq_lock);
}

static void * taskq_thread(void *arg)
{
	taskq_t *tq = arg;
	task_t *t;

	pthread_mutex_lock(&tq->tq_lock);
	while (tq->tq_flags & TASKQ_ACTIVE) {
		if ((t = tq->tq_task.task_next) == &tq->tq_task) {
			if (--tq->tq_active == 0)
				pthread_cond_broadcast(&tq->tq_wait_cv);
			pthread_cond_wait(&tq->tq_dispatch_cv, &tq->tq_lock);
			tq->tq_active++;
			continue;
		}
		//remove t from the task list
		t->task_prev->task_next = t->task_next;
		t->task_next->task_prev = t->task_prev;
		pthread_mutex_unlock(&tq->tq_lock);

		//using a rwlock here since we may have multiple
		//service threads running once tq_lock is released
		//like the above line
		pthread_rwlock_rdlock(&tq->tq_threadlock);
		t->task_func(t->task_arg);
		pthread_rwlock_unlock(&tq->tq_threadlock);

		pthread_mutex_lock(&tq->tq_lock);
		//t is done, now free t
		task_free(tq, t);
	}
	tq->tq_nthreads--;
	pthread_cond_broadcast(&tq->tq_wait_cv);
	pthread_mutex_unlock(&tq->tq_lock);
	return (NULL);
}

taskq_t * taskq_create(const char *name, int nthreads, short pri,
	int minalloc, int maxalloc, unsigned int flags)
{
	taskq_t *tq = malloc(sizeof (taskq_t));
	int t;
#if 0
	if (flags & TASKQ_THREADS_CPU_PCT) {
		int pct;
		//ASSERT3S(nthreads, >=, 0);
		//ASSERT3S(nthreads, <=, 100);
		pct = MIN(nthreads, 100);
		pct = MAX(pct, 0);

		nthreads = (sysconf(_SC_NPROCESSORS_ONLN) * pct) / 100;
		nthreads = MAX(nthreads, 1);	/* need at least 1 thread */
	} else {
		ASSERT3S(nthreads, >=, 1);
	}
#endif

	pthread_rwlock_init(&tq->tq_threadlock, NULL);
	pthread_mutex_init(&tq->tq_lock, NULL);
	pthread_cond_init(&tq->tq_dispatch_cv, NULL);
	pthread_cond_init(&tq->tq_wait_cv, NULL);
	pthread_cond_init(&tq->tq_maxalloc_cv, NULL);
	tq->tq_flags = flags | TASKQ_ACTIVE;
	tq->tq_active = nthreads;
	tq->tq_nthreads = nthreads;
	tq->tq_minalloc = minalloc;
	tq->tq_maxalloc = maxalloc;
	tq->tq_task.task_next = &tq->tq_task;
	tq->tq_task.task_prev = &tq->tq_task;
	tq->tq_threadlist = malloc(nthreads * sizeof (pthread_t));

	if (flags & TASKQ_PREPOPULATE) {
		pthread_mutex_lock(&tq->tq_lock);
		while (minalloc-- > 0)
			task_free(tq, task_alloc(tq, 0));
		pthread_mutex_unlock(&tq->tq_lock);
	}

	for (t = 0; t < nthreads; t++)
		(void) pthread_create(
			&tq->tq_threadlist[t], NULL, taskq_thread, tq);

	return (tq);
}

void taskq_destroy(taskq_t *tq)
{
	int t;
	int nthreads = tq->tq_nthreads;

	taskq_wait(tq);

	pthread_mutex_lock(&tq->tq_lock);

	tq->tq_flags &= ~TASKQ_ACTIVE;
	pthread_cond_broadcast(&tq->tq_dispatch_cv);

	while (tq->tq_nthreads != 0)
		pthread_cond_wait(&tq->tq_wait_cv, &tq->tq_lock);

	tq->tq_minalloc = 0;
	while (tq->tq_nalloc != 0) {
		//ASSERT(tq->tq_freelist != NULL);
		task_free(tq, task_alloc(tq, 0));
	}

	pthread_mutex_unlock(&tq->tq_lock);

	for (t = 0; t < nthreads; t++)
		(void) pthread_join(tq->tq_threadlist[t], NULL);

	free(tq->tq_threadlist);

	pthread_rwlock_destroy(&tq->tq_threadlock);
	pthread_mutex_destroy(&tq->tq_lock);
	pthread_cond_destroy(&tq->tq_dispatch_cv);
	pthread_cond_destroy(&tq->tq_wait_cv);
	pthread_cond_destroy(&tq->tq_maxalloc_cv);

	free(tq);
}

int taskq_member(taskq_t *tq, void *t)
{
	int i;

	if (taskq_now)
		return (1);

	for (i = 0; i < tq->tq_nthreads; i++)
		if (tq->tq_threadlist[i] == (pthread_t)(unsigned long)t)
			return (1);

	return (0);
}

void
system_taskq_init(void)
{
	system_taskq = taskq_create("system_taskq", 64, 60, 4, 512,
	    TASKQ_DYNAMIC | TASKQ_PREPOPULATE);
}

void
system_taskq_fini(void)
{
	taskq_destroy(system_taskq);
	system_taskq = NULL; /* defensive */
}
