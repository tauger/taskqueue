CC=gcc
CFLAGS=-std=gnu99
LDFLAGS=-lrt
DEPS = taskq.h
OBJS = taskq.o test.o

all: malloc tcmalloc

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

malloc: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o tq_test_glibc
	$(CC) $(LDFLAGS) $(OBJS) -lprofiler -o tq_test_glibc_profiler

tcmalloc: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -ltcmalloc -o tq_test_tcmalloc
	$(CC) $(LDFLAGS) $(OBJS) -ltcmalloc -lprofiler -o tq_test_tcmalloc_profiler

clean:
	rm -f *.o tq_test_glibc tq_test_tcmalloc tq_test_tcmalloc_profiler tq_test_glibc_profiler


