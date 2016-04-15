#ifdef PROFILE

#include <pthread.h>
#include <sys/queue.h>

typedef enum {
	PERF_CYCLES,
	PERF_LAST
} perf_type_t;

struct perf_counter {
	SLIST_ENTRY(perf_counter)	counters;

	uint64_t	last;

	uint64_t	min;
	uint64_t	max;
	uint64_t	sum;
	uint64_t	sumsq;

	int		fd;
	perf_type_t	type;
};

struct perf {
	SLIST_ENTRY(perf)				perfs;
	SLIST_HEAD(perf_counter_head, perf_counter)	counter_head;

	int		fd;

	uint64_t	workdone;
	uint64_t	offset;
	uint64_t	measures;
	double		fuzz;

	int		tid;
	const char	*name;
};

struct perf * perf_init(const char *, unsigned int, ...);
void perf_finish(struct perf *);
void perf_measure(struct perf *, const uint64_t);
void perf_unpause(struct perf *, const uint64_t);
void perf_pause(struct perf *);

#define NUMARGS(...)		(sizeof((int[]){__VA_ARGS__})/sizeof(int))

#define PERF_STORE(x)		static __thread struct perf *perf_##x;
#define PERF_INIT(x, ...)	if (!perf_##x) \
					perf_##x = perf_init(#x, NUMARGS(__VA_ARGS__), __VA_ARGS__)
#define PERF_MEASURE(x, y)	perf_measure(perf_##x, y)
#define PERF_UNPAUSE(x,y)	perf_unpause(perf_##x, y)
#define PERF_PAUSE(x)		perf_pause(perf_##x)
#else
#define PERF_STORE(x)
#define PERF_INIT(...)		do { } while (0)
#define PERF_MEASURE(...)	do { } while (0)
#define PERF_UNPAUSE(...)	do { } while (0)
#define PERF_PAUSE(...)		do { } while (0)
#endif
