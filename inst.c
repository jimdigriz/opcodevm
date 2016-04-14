#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <limits.h>

#define MAX_DEPTH 10

/* perf_event_open() is per thread, otherwise read() returns the old value */
static __thread int once = 0;
static __thread int fd;
static __thread int depth = -1;

struct stopwatch {
	long long count;
};
static __thread struct stopwatch stopwatch[MAX_DEPTH];

static int gettid()
{
	return syscall(__NR_gettid);
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* perf_event_open(2) */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
				int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static void init()
{
	struct perf_event_attr pe = {0};

	pe.type = PERF_TYPE_HARDWARE;
	pe.size = sizeof(struct perf_event_attr);
	pe.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = perf_event_open(&pe, 0, -1, -1, 0);
	if (fd == -1)
		err(EX_OSERR, "perf_event_open()");

	if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_RESET)");

	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_ENABLE)");
}

static void fini()
{
	close(fd);
}

/* http://linuxgazette.net/151/melinte.html */
void __cyg_profile_func_enter(void *func, void *callsite)
{
	(void)func;
	(void)callsite;

	if (!once) {
		once = 1;
		init();
	}

	depth++;
	if (depth >= MAX_DEPTH) {
		warnx("inception");
		abort();
	}

	if (read(fd, &stopwatch[depth].count, sizeof(long long)) != sizeof(long long))
		err(EX_SOFTWARE, "short read (enter)");
}

void __cyg_profile_func_exit(void *func, void *callsite)
{
	(void)func;
	(void)callsite;

	long long count;

	if (read(fd, &count, sizeof(long long)) != sizeof(long long))
		err(EX_SOFTWARE, "short read (exit)");

	count = (count < stopwatch[depth].count)
			? count + (LLONG_MAX - stopwatch[depth].count)
			: count - stopwatch[depth].count;

	pid_t pid = getpid();
	pid_t tid = gettid();

	ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
	pthread_mutex_lock(&mutex);

	fprintf(stderr, "%dT%d@%d: %p PERF_COUNT_HW_REF_CPU_CYCLES = %lld\n", pid, tid, depth, func, count);

	pthread_mutex_unlock(&mutex);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

	depth--;

	if (depth < 0)
		fini();
}
