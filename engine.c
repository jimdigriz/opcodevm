#include <unistd.h>
#include <err.h>
#include <inttypes.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "engine.h"

/* order is important, 'fastest' needs to be at the end */
static const char *opobjs[] = {
	"code/bswap/c.so",
	"code/bswap/x86_64.so",
	"code/bswap/opencl.so",
	NULL,
};

static long pagesize;
static unsigned int mlocksize;

void engine_init() {
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

/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
#define CODE(x) case x: goto x
#define NEXT switch ((*ip++).code) \
{ \
	CODE(RET); \
	CODE(BSWAP); \
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

	for (uint64_t i = 0; i < data[0].numrec; i += mlocksize / data[0].reclen) {
		struct data d = {
			.addr	= &((uint8_t *)data[0].addr)[i * data[0].reclen],
			.reclen	= data[0].reclen,
			.endian = data[0].endian,
		};
		d.numrec = ((data[0].numrec - i) > mlocksize / data[0].reclen)
				? mlocksize / data[0].reclen
				: data[0].numrec - i;

		if (mlock(d.addr, d.numrec * d.reclen) == -1)
			err(EX_OSERR, "mlock(%" PRIu64 ")", d.numrec * d.reclen);

		struct insn *ip = program->insns;

		NEXT;

		RET:
			if (munlock(d.addr, d.numrec * d.reclen))
				err(EX_OSERR, "munlock(%" PRIu64 ")", d.numrec * d.reclen);
			continue;
		BSWAP:
			bswap(&d);
			NEXT;
	}
}
