#include <unistd.h>
#include <err.h>
#include <inttypes.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>

#include "engine.h"

/* order is important, 'fastest' needs to be at the end */
static const char *opobjs[] = {
	"code/bswap/c.so",
	"code/bswap/x86_64.so",
	"code/bswap/opencl.so",
	NULL,
};

static long instances = 1;
static long pagesize;
static unsigned int mlocksize;

void engine_init() {
	errno = 0;
	if (getenv("INSTANCES"))
		instances = strtol(getenv("INSTANCES"), NULL, 10);
	if (errno == ERANGE || instances < 0)
		err(EX_DATAERR, "invalid INSTANCES");
	if (instances == 0)
		instances = sysconf(_SC_NPROCESSORS_ONLN);
	if (instances == -1)
		err(EX_OSERR, "sysconf(_SC_NPROCESSORS_ONLN)");

	pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize == -1)
		err(EX_OSERR, "sysconf(_SC_PAGESIZE)");

	const long l2_cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
	if (l2_cache_size == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

	struct rlimit rlim;
	if (getrlimit(RLIMIT_MEMLOCK, &rlim))
		err(EX_OSERR, "getrlimit(RLIMIT_MEMLOCK)");

	mlocksize = ((unsigned int)l2_cache_size/2 < rlim.rlim_cur)
			? (unsigned int)l2_cache_size/2
			: rlim.rlim_cur;
	mlocksize = mlocksize / instances;
	mlocksize = mlocksize - (mlocksize % pagesize);
	if (mlocksize < pagesize)
		errx(EX_SOFTWARE, "mlocksize is less than pagesize");

	struct op *ops[NCODES];

	ops[BSWAP - 1] = bswap_ops();

	for (const char **l = opobjs; *l; l++) {
		void *handle = dlopen(*l, RTLD_NOW|RTLD_LOCAL);
		if (!handle) {
			warnx("dlopen(%s): %s\n", *l, dlerror());
			abort();
		}

		dlerror();

		struct op *(*init)() = (struct op *(*)())(intptr_t)dlsym(handle, "init");
		char *error = dlerror();
		if (error) {
			warnx("dlsym(%s): %s\n", *l, error);
			abort();
		}

		struct op *op = init();
		if (!op) {
			dlclose(handle);
			continue;
		}

#define POPULATE(x)	if (op->u##x[i]) \
				ops[op->code - 1]->u##x[i] = op->u##x[i]
		/* not 3 as that is to remain a row of 0's */
		for (unsigned int i = 0; i < 2; i++) {
			POPULATE(16);
			POPULATE(32);
			POPULATE(64);
		}
	}

#define BACKFILL(x)	if (!ops[i]->u##x[0] && ops[i]->u##x[1]) \
				ops[i]->u##x[0] = ops[i]->u##x[1]
	for (unsigned int i = 0; i < NCODES; i++) {
		BACKFILL(16);
		BACKFILL(32);
		BACKFILL(64);

#ifndef NDEBUG
#	define CTERM(x)	if (ops[i]->u##x[2]) {							\
				warnx("ops[%d]->x table does not have terminating NULL", i);	\
				abort();							\
			}

		CTERM(16);
		CTERM(32);
		CTERM(64);
#endif
	}
}

struct engine_instance_info {
	struct program	*program;
	struct data	*data;
	pthread_t	thread;
	unsigned int	instance;
};

static void * engine_instance(void *arg)
{
	struct engine_instance_info *eii = arg;

	struct data d = {
		.reclen		= eii->data[0].reclen,
		.endian		= eii->data[0].endian,
	};

	uint64_t pos = (mlocksize / d.reclen) * eii->instance;
	while (pos < eii->data[0].numrec) {
		d.addr		= &((uint8_t *)eii->data[0].addr)[pos * eii->data[0].reclen];
		d.numrec	= ((eii->data[0].numrec - pos) > mlocksize / eii->data[0].reclen)
					? mlocksize / eii->data[0].reclen
					: eii->data[0].numrec - pos;

		const unsigned int len = d.numrec * d.reclen;

		if (mlock(d.addr, len) == -1)
			err(EX_OSERR, "mlock(%u)", len);

		/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
#		define CODE(x) case x: goto x
#		define NEXT switch ((*ip++).code) \
		{ \
			CODE(RET); \
			CODE(BSWAP); \
		}

		struct insn *ip = eii->program->insns;

		NEXT;

		RET:
			if (munlock(d.addr, len) == -1)
				err(EX_OSERR, "mlock(%u)", len);
			pos += (mlocksize / d.reclen) * instances;
			continue;
		BSWAP:
			bswap(&d);
			NEXT;
	}

	return NULL;
}

void engine_run(struct program *program, struct data *data)
{
	if (program->insns[program->len - 1].code != RET) {
		warnx("program needs to end with RET\n");
		abort();
	}

	for (unsigned int i = 0; data[i].addr; i++) {
		if ((uintptr_t)data[i].addr % pagesize) {
			warnx("data[%d] is not page aligned\n", i);
			abort();
		}
	}

	struct engine_instance_info *eii = calloc(instances, sizeof(struct engine_instance_info));

	for (unsigned int i = 0; i < instances; i++) {
		eii[i].program	= program;
		eii[i].data	= data;
		eii[i].instance	= i;

		if (instances == 1) {
			engine_instance(&eii[i]);
		} else {
			if (pthread_create(&eii[i].thread, NULL, engine_instance, &eii[i]))
				err(EX_OSERR, "pthread_create()");
		}
	}

	if (instances > 1) {
		for (int i = 0; i < instances; i++) {
			errno =  pthread_join(eii[i].thread, NULL);
			if (errno)
				err(EX_OSERR, "pthread_join()");
		}
	}

	free(eii);
}
