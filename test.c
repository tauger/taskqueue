/*
 * Copyright 2011 Grant Zhang
 *
 * taskq implementation based on PTHREAD
 * Modified from Solaris ZFS libzpool taskq implementation
 * Compile with: gcc -std=gnu99 taskq.c test.c -lrt -o tq_test
 */

#include <stdlib.h>
#include <stdio.h>

#include "taskq.h"

extern taskq_t *system_taskq;

void mywork(void *arg)
{
#if 1
	int loops = *(int *)arg;
	char **pp = calloc(loops, sizeof(char *));
	for (int i=0; i<loops; i++) {
		pp[i] = (char *)malloc(64);
	}
	//printf("task %d: done malloc()\n", loops);
	for (int i=0; i<loops; i++) {
		free(pp[i]);
	}		
	//printf("task %d: done free()\n", loops);
#endif
}

int main(int argc, char **argv)
{
	int i = 0;
	int *a;
	int ntasks;

	if (argc!=2) {
		printf("usage: tq_test #_of_tasks\n");
		exit(1);
	}

	ntasks = atoi(argv[1]);
	a = calloc(ntasks, sizeof(int));

	system_taskq_init();

	/* now test how the taskq functions */
	for (i=0; i<ntasks; i++)
	{
		a[i] = i;
		taskq_dispatch(system_taskq, mywork, a+i, TQ_FRONT);
	}

	system_taskq_fini();
	return 0;
}
