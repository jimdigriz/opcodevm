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
#include <assert.h>

#include "engine.h"

struct opcode opcode[256];

static const char *libs[] = {
	/* first load the code points (order not important) */
	"code/bswap.so",

	/* now the ops (order is important, descending by 'speed') */
	"code/bswap/opencl.so",
	"code/bswap/x86_64.so",
	"code/bswap/c.so",
	NULL,
};

static long instances = 1;
static long pagesize, l1_dcache_linesize, l2_cache_size;

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

	l1_dcache_linesize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	if (l1_dcache_linesize == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL1_DCACHE_LINESIZE)");
	if (pagesize % l1_dcache_linesize)
		errx(EX_SOFTWARE, "pagesize %% l1_dcache_linesize");

	l2_cache_size = sysconf(_SC_LEVEL2_CACHE_SIZE);
	if (l2_cache_size == -1)
		err(EX_OSERR, "sysconf(_SC_LEVEL2_CACHE_SIZE)");

	struct rlimit rlim;
	if (getrlimit(RLIMIT_MEMLOCK, &rlim))
		err(EX_OSERR, "getrlimit(RLIMIT_MEMLOCK)");

	for (const char **l = libs; *l; l++) {
		void *handle = dlopen(*l, RTLD_NOW|RTLD_LOCAL);
		if (!handle) {
			warnx("dlopen(%s): %s\n", *l, dlerror());
			abort();
		}
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

	uint64_t minrec = UINT64_MAX;
	uint8_t reclen = 0;
	for (unsigned int i = 0; i < eii->ndata; i++) {
		d[i].type	= eii->data[i].type;
		d[i].reclen	= eii->data[i].reclen;

		if (eii->data[i].numrec < minrec)
			minrec = eii->data[i].numrec;

		reclen += eii->data[i].reclen;
	}

	uint64_t *M = calloc(eii->program->mwords, sizeof(uint64_t));
	if (!M)
		errx(EX_OSERR, "calloc(M)");

	/* we divide by two so not to saturate the cache (pagesize aligned for OpenCL) */
	const uint64_t stride = (l2_cache_size / reclen / 2)
					- ((l2_cache_size / reclen / 2) % pagesize);

	for (uint64_t pos = eii->instance * stride; pos < minrec; pos += instances * stride) {
		const uint64_t numrec = ((minrec - pos) > stride) ? stride : minrec - pos;

		for (unsigned int i = 0; i < eii->ndata; i++) {
			d[i].addr	= &((uint8_t *)eii->data[i].addr)[pos * eii->data[i].reclen];
			d[i].numrec	= numrec;
		}

		/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
#		define LABL(x, y) x##__##y
#		define CASE(x, y) case OPCODE(x,y): goto LABL(x,y)
#		define NEXT switch ((*ip++).code)	\
		{					\
			CASE(MISC, BSWP);		\
			case OC_RET:	goto RET;	\
			default:	goto SIGILL;	\
		}
#		define CALL(x, ...)	offset = 0;					\
					opcode[x].call(&offset, d, __VA_ARGS__);	\
					assert(offset == numrec);

		uint64_t offset;

		struct insn *ip = eii->program->insns;

		NEXT;

		LABL(MISC, BSWP):
			CALL(OPCODE(MISC, BSWP), ip->k);
			NEXT;
		RET:
			continue;
		SIGILL:
			errx(EX_USAGE, "SIGILL %d (class=%d)", ip->code, OC_CLASS(ip->code));
	}

	free(M);

	free(d);

	return NULL;
}

void engine_run(struct program *program, int ndata, struct data *data)
{
	if (program->insns[program->len - 1].code != OC_RET)
		errx(EX_USAGE, "program needs to end with OC_RET");

	if (ndata == 0)
		errx(EX_USAGE, "ndata needs to be greater than 0");

	for (int i = 0; i < ndata; i++)
		if ((uintptr_t)data[i].addr % pagesize)
			errx(EX_SOFTWARE, "data[%d] is not page aligned", i);

	if (program->mwords == 0)
		errx(EX_USAGE, "mwords needs to be greater than 0");

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
