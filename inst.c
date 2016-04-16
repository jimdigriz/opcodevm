#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <err.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <math.h>

#include "inst.h"

pthread_mutex_t perf_mutex = PTHREAD_MUTEX_INITIALIZER;
SLIST_HEAD(perf_head, perf) perf_head = SLIST_HEAD_INITIALIZER(perf_head);

static int gettid()
{
	return syscall(__NR_gettid);
}

/* perf_event_open(2) */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
				int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

struct perf * perf_init(const char *name, unsigned int perfn, ...)
{
	struct perf_event_attr pe = {0};

	pe.size = sizeof(struct perf_event_attr);
	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;
	pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING | PERF_FORMAT_GROUP;

	struct perf *perf = calloc(1, sizeof(struct perf));

	SLIST_INIT(&perf->counter_head);
	perf->fd = -1;
	perf->tid = gettid();
	perf->name = name;
	perf->ncounters = perfn;

	assert(perfn > 0 && perfn <= PERF_LAST);

	va_list ap;
	va_start(ap, perfn);

	struct perf_counter *counter_prev = SLIST_FIRST(&perf->counter_head);
	while (perfn-->0) {
		perf_type_t type = va_arg(ap, perf_type_t);

		switch (type) {
		case PERF_CYCLES:
			pe.type = PERF_TYPE_HARDWARE;
			pe.config = PERF_COUNT_HW_REF_CPU_CYCLES;
			break;
		case PERF_LAST:
			warnx("PERF_LAST is not valid");
			abort();
		}

		struct perf_counter *counter = calloc(1, sizeof(struct perf_counter));
		if (counter_prev) {
			SLIST_INSERT_AFTER(counter_prev, counter, counters);
		} else {
			SLIST_INSERT_HEAD(&perf->counter_head, counter, counters);
		}
		counter_prev = counter;

		counter->fd = perf_event_open(&pe, 0, -1, perf->fd, 0);
		if (counter->fd == -1)
			err(EX_OSERR, "perf_event_open()");

		if (perf->fd == -1)
			perf->fd = counter->fd;

		counter->min	= UINT64_MAX;
		counter->type	= type;
	}

	if (ioctl(perf->fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_RESET)");

	if (pthread_mutex_lock(&perf_mutex) != 0)
		err(EX_OSERR, "pthread_mutex_lock(perf_mutex)");

	SLIST_INSERT_HEAD(&perf_head, perf, perfs);

	if (pthread_mutex_unlock(&perf_mutex) != 0)
		err(EX_OSERR, "pthread_mutex_unlock(perf_mutex)");

	return perf;
}

void perf_measure(struct perf *perf, const uint64_t offset)
{
	struct read_format {
		uint64_t		nr;
		uint64_t		time_enabled;
		uint64_t		time_running;
		struct {
			uint64_t	value;
		} values[PERF_LAST * sizeof(uint64_t)];
	} data;

	if (read(perf->fd, &data, sizeof(struct read_format)) == -1)
		err(EX_SOFTWARE, "read(perf->fd)");

	assert(data.nr == perf->ncounters);

	uint64_t workdone = offset - perf->offset;
	perf->workdone += workdone;
	perf->measures++;
	perf->fuzz += (data.time_enabled == data.time_running)
				? data.time_enabled / data.time_running : 1;

	unsigned int i = 0;
	struct perf_counter *counter;
	SLIST_FOREACH(counter, &perf->counter_head, counters) {
		int64_t delta		= data.values[i].value - counter->last;
		assert(delta >= 0);
		counter->last		= data.values[i].value;
		i++;

		counter->sum		+= delta;
		counter->sumsq		+= delta * delta;

		uint64_t cycle		= delta / workdone;
		assert(cycle > 0);

		if (cycle > counter->max)
			counter->max = cycle;
		if (cycle < counter->min)
			counter->min = cycle;
	}
}

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static void __attribute((destructor)) report()
{
	/* ordered with perf_type_t */
	const char *perf_type_name[] = {
		"cycles",
	};
	assert(ARRAY_SIZE(perf_type_name) == PERF_LAST);

	struct perf *perf;
	SLIST_FOREACH(perf, &perf_head, perfs) {
		char *est = "";

		double acc = perf->fuzz / perf->measures;
		if (acc != 1.00) {
			perf->workdone *= acc;
			est = " [est]";
		}

		fprintf(stderr, "\n%s[%d]:\nworkdone=%" PRIu64 "%s\n", perf->name, perf->tid, perf->workdone, est);

		struct perf_counter *counter;
		SLIST_FOREACH(counter, &perf->counter_head, counters) {
			close(counter->fd);

			uint64_t avg = counter->sum / perf->workdone;
			/* this seems meaningless as the variance is usually low */
			uint64_t sdd = sqrt((counter->sumsq - (counter->sum * counter->sum) / perf->workdone) / (perf->workdone * (perf->workdone - 1)));
			fprintf(stderr, "%s=%" PRIu64 ":%" PRIu64 "~%" PRIu64 ":%" PRIu64 "\n",
					perf_type_name[counter->type], counter->min, avg, sdd, counter->max);
		}
	}
}

void perf_unpause(struct perf *perf, const uint64_t offset)
{
	if (offset != UINT64_MAX)
		perf->offset = offset;

	if (ioctl(perf->fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_ENABLE)");
}

void perf_pause(struct perf *perf)
{
	if (ioctl(perf->fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_DISABLE)");
}
