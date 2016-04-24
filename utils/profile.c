#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <dlfcn.h>
#include <err.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <math.h>

#include "engine.h"

static long cycles = 1000;
static long nbestof = 3;
static long length = 0;
static long pagesize;

extern SLIST_HEAD(opcode_list, opcode) opcode_list;

struct result {
	uint64_t	*bestof;
	uint64_t	max;
	uint64_t	sum;

	/* http://www.johndcook.com/blog/standard_deviation/ */
	double		m;
	double		s;
};

/* perf_event_open(2) */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
				int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static int perf_init()
{
	struct perf_event_attr pe = {0};

	pe.size = sizeof(struct perf_event_attr);
	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;
	pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

	pe.type = PERF_TYPE_HARDWARE;
	pe.config = PERF_COUNT_HW_REF_CPU_CYCLES;

	int fd = perf_event_open(&pe, 0, -1, -1, 0);
	if (fd == -1)
		err(EX_OSERR, "perf_event_open()");

	return fd;
}

static void perf_fini(int fd)
{
	close(fd);
}

static uint64_t perf_measure(int fd)
{
	struct read_format {
		uint64_t	value;
		uint64_t	time_enabled;
		uint64_t	time_running;
	} data;

	if (read(fd, &data, sizeof(struct read_format)) == -1)
		err(EX_SOFTWARE, "read(perf->fd)");

	if (data.time_enabled != data.time_running)
		return UINT64_MAX;

	return data.value;
}

static void perf_reset(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_RESET)");
}

static inline void perf_unpause(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_ENABLE)");
}

static inline void perf_pause(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) == -1)
		err(EX_OSERR, "ioctl(PERF_EVENT_IOC_DISABLE)");
}

static void profile_engine_init() {
	errno = 0;
	if (getenv("CYCLES"))
		cycles = strtol(getenv("CYCLES"), NULL, 10);
	if (errno == ERANGE || cycles < 0)
		err(EX_DATAERR, "invalid CYCLES");

	if (getenv("BESTOF"))
		nbestof = strtol(getenv("BESTOF"), NULL, 10);
	if (errno == ERANGE || nbestof < 0)
		err(EX_DATAERR, "invalid BESTOF");

	if (cycles < nbestof)
		errx(EX_DATAERR, "CYCLES is less than BESTOF");

	if (getenv("LENGTH"))
		length = strtol(getenv("LENGTH"), NULL, 10);
	if (errno == ERANGE || length < 0)
		err(EX_DATAERR, "invalid LENGTH");
	if (length == 0) {
		length = sysconf(_SC_LEVEL2_CACHE_SIZE);
		if (length == -1)
			err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

		/* default half of L2 cache to not saturate it */
		length /= 2;
	}

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	if (length % pagesize)
		errx(EX_DATAERR, "LENGTH %% PAGESIZE != 0");
}

static struct result * perf_benchmark(int p, struct opcode *opcode)
{
	struct result *result = calloc(1, sizeof(struct result));
	if (!result)
		err(EX_OSERR, "calloc(result)");

	result->bestof = calloc(nbestof, sizeof(uint64_t));
	if (!result->bestof)
		err(EX_OSERR, "calloc(result->bestof)");
	for (int i = 0; i < nbestof; i++)
		result->bestof[i] = UINT64_MAX;

	perf_reset(p);

	uint64_t before = 0;

	for (unsigned int i = 0; i < cycles; i++) {
		perf_unpause(p);

		if (opcode)
			opcode->profile();

		perf_pause(p);

		uint64_t after = perf_measure(p);
		/* perf counter contention */
		if (after == UINT64_MAX)
			goto reset;

		/* handle counter wraps (unknown maximums) */
		if (before > after)
			goto retry;

		unsigned int delta = after - before;
		/* silly result */
		if (delta == 0)
			goto retry;

		if (result->max < delta)
			result->max = delta;
		result->sum += delta;

		for (int n = 0; n < nbestof; n++) {
			if (result->bestof[n] > delta) {
				result->bestof[n] = delta;
				break;
			}
		}

		if (i > 0) {
			double mlast = result->m;
			result->m = mlast + (delta - mlast) / (i + 1);
			result->s += (delta - mlast) * (delta - result->m);
		} else {
			result->m = delta;
			result->s = 0;
		}

		before = after;
		continue;

reset:
#ifndef NDEBUG
		fprintf(stderr, "reset, ");
#endif
		perf_reset(p);
		after = 0;
retry:
#ifndef NDEBUG
		fprintf(stderr, "retry\n");
#endif
		before = after;
		i--;
	}

	return result;
}

static void print(struct result *result)
{
	for (int i = 0; i < nbestof; i++)
		printf(" %" PRIu64, result->bestof[i]);

	uint64_t avg = result->sum / cycles;
	uint64_t sdd = sqrt(result->s / (cycles - 1));

	printf(":%" PRIu64 "~%" PRIu64 ":%" PRIu64 "\n", avg, sdd, result->max);
}

int main(int argc, char **argv)
{
	if (argc != 3)
		errx(EX_USAGE, "%s code/<opcode>.so code/<opcode>/<imp>.so", argv[0]);

	for (int i = 1; i < argc; i++) {
		void *handle = dlopen(argv[i], RTLD_NOW|RTLD_LOCAL);
		if (!handle)
			errx(EX_SOFTWARE, "dlopen(%s): %s\n", argv[i], dlerror());
	}

	profile_engine_init();
	int p = perf_init();

	struct result *noop = perf_benchmark(p, NULL);
	printf("noop perf:\n");
	print(noop);

	printf("\n");

	struct opcode *opcode = SLIST_FIRST(&opcode_list);
	opcode->profile_init(length, 32);

	struct result *op = perf_benchmark(p, opcode);
	printf("op perf:\n");
	print(op);

	perf_fini(p);

	opcode->profile_fini();

	return 0;
}
