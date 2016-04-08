#include <unistd.h>
#include <err.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "engine.h"

/* order is important, 'fastest' needs to be at the end */
static const char *opobjs[] = {
	"code/bswap/c.so",
	"code/bswap/x86_64.so",
	NULL,
};

void engine_init() {
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

#define POPULATE(x)	if (op->u##x[i] && !ops[op->code - 1]->u##x[i]) \
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
#ifndef NDEBUG
	for (int i = 0; data[i].addr; i++) {
		if ((uintptr_t)data[i].addr % getpagesize() != 0) {
			warnx("data[%d] is not page aligned\n", i);
			abort();
		}
	}
#endif

	/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
	struct insn *ip = program->insns;

	NEXT;

	RET:
		return;
	BSWAP:
		bswap(&data[0]);
		NEXT;
}
