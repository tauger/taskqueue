/*
 * Copyright 2011 Grant Zhang
 *
 * taskq implementation based on PTHREAD
 * Modified from Solaris ZFS libzpool taskq implementation
 * Compile with: gcc taskq.c -lrt -o tq_test
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifndef TASKQ_H
#define TASKQ_H

#define	TASKQ_PREPOPULATE	0x0001
#define	TASKQ_CPR_SAFE		0x0002	/* Use CPR safe protocol */
#define	TASKQ_DYNAMIC		0x0004	/* Use dynamic thread scheduling */
#define	TASKQ_THREADS_CPU_PCT	0x0008	/* Scale # threads by # cpus */
#define	TASKQ_DC_BATCH		0x0010	/* Mark threads as batch */
#define	TASKQ_ACTIVE	0x00010000

#define	TQ_SLEEP	0x0	/* Can block for memory */
#define	TQ_NOSLEEP	0x1	/* cannot block for memory; may fail */
#define	TQ_NOQUEUE	0x02	/* Do not enqueue if can't dispatch */
#define	TQ_FRONT	0x08	/* Queue in front */

typedef struct taskq taskq_t;
typedef void (task_func_t)(void *);
typedef unsigned int taskqid_t;

typedef struct task {
	struct task	*task_next;
	struct task	*task_prev;
	task_func_t	*task_func;
	void		*task_arg;
} task_t;

struct taskq {
	pthread_mutex_t	tq_lock;
	pthread_rwlock_t tq_threadlock;
	pthread_cond_t	tq_dispatch_cv;
	pthread_cond_t	tq_wait_cv;
	pthread_t	*tq_threadlist;
	int		tq_flags;
	int		tq_active;
	int		tq_nthreads;
	int		tq_nalloc;
	int		tq_minalloc;
	int		tq_maxalloc;
	pthread_cond_t	tq_maxalloc_cv;
	int		tq_maxalloc_wait;
	task_t		*tq_freelist;
	task_t		tq_task;
};

/* function prototypes */
// Caller has to hold tq->tq_lock
static task_t * task_alloc(taskq_t *, int );
static void task_free(taskq_t *, task_t *);
taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, unsigned int);

/* waiting for pending task to complete */
void taskq_wait(taskq_t *);
static void * taskq_thread(void *);

/*ARGSUSED*/
taskq_t * taskq_create(const char *, int, short,
	int, int, unsigned int);

void taskq_destroy(taskq_t *);
int taskq_member(taskq_t *, void *);
void system_taskq_init(void);
void system_taskq_fini(void);

#endif /* TASKQ_H */
