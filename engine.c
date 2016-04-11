#include <unistd.h>
#include <err.h>
#include <inttypes.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
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
static long l2_cache_size;

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

	l2_cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
	if (l2_cache_size == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

	struct rlimit rlim;
	if (getrlimit(RLIMIT_MEMLOCK, &rlim))
		err(EX_OSERR, "getrlimit(RLIMIT_MEMLOCK)");

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

#		define POPULATE(x)	if (op->u##x[i]) \
						ops[op->code - 1]->u##x[i] = op->u##x[i]
		/* not 3 as that is to remain a row of 0's */
		for (unsigned int i = 0; i < 2; i++) {
			POPULATE(16);
			POPULATE(32);
			POPULATE(64);
		}
	}

#	define BACKFILL(x)	if (!ops[i]->u##x[0] && ops[i]->u##x[1]) \
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
	unsigned int	ndata;
	pthread_t	thread;
	unsigned int	instance;
};

static void * engine_instance(void *arg)
{
	struct engine_instance_info *eii = arg;

	struct data *d = calloc(eii->ndata, sizeof(struct data));
	if (!d)
		errx(EX_OSERR, "calloc(d)");

	uint64_t numrec = UINT64_MAX;
	uint8_t reclen = 0;
	for (unsigned int i = 0; i < eii->ndata; i++) {
		d[i].type	= eii->data[i].type;
		d[i].reclen	= eii->data[i].reclen;

		if (eii->data[i].numrec < numrec)
			numrec = eii->data[i].numrec;

		if (eii->data[i].reclen > reclen)
			reclen = eii->data[i].reclen;
	}

	uint64_t *R = calloc(eii->program->rwords, sizeof(uint64_t));
	if (!R)
		errx(EX_OSERR, "calloc(R)");

	/* we divide by two so not to saturate the cache */
	const uint64_t stride = l2_cache_size / reclen / 2;

	for (uint64_t pos = eii->instance * stride; pos < numrec; pos += stride) {
		for (unsigned int i = 0; i < eii->ndata; i++) {
			d[i].addr	= &((uint8_t *)eii->data[i].addr)[pos * eii->data[i].reclen];
			d[i].numrec	= ((numrec - pos) > stride) ? stride : numrec - pos;
		}

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
			continue;
		BSWAP:
			bswap(d, ip->k);
			NEXT;
	}

	free(R);

	free(d);

	return NULL;
}

void engine_run(struct program *program, int ndata, struct data *data)
{
	if (program->insns[program->len - 1].code != RET)
		errx(EX_USAGE, "program needs to end with RET");

	if (ndata == 0)
		errx(EX_USAGE, "ndata needs to be greater than 0");

	for (int i = 0; i < ndata; i++)
		if ((uintptr_t)data[i].addr % pagesize)
			errx(EX_SOFTWARE, "data[%d] is not page aligned", i);

	if (program->rwords == 0)
		errx(EX_USAGE, "rwords needs to be greater than 0");

	struct engine_instance_info *eii = calloc(instances, sizeof(struct engine_instance_info));

	for (unsigned int i = 0; i < instances; i++) {
		eii[i].program	= program;
		eii[i].ndata	= ndata;
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
