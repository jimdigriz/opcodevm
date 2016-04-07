#include <unistd.h>
#include <err.h>
#include <sysexits.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "engine.h"

const char *opobjs[] = {
	"code/bswap/c.so",
	"code/bswap/x86_64.so",
	NULL,
};

/* RET is not in this list, so -1 the enum value for offset */
struct op ops[NCODES];

void engine_init() {
	for (const char **l = opobjs; *l; l++) {
		void *handle = dlopen(*l, RTLD_NOW);
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
		if (!op)
			continue;

		if (op->u16)
			ops[op->code - 1].u16 = op->u16;
		if (op->u32)
			ops[op->code - 1].u32 = op->u32;
		if (op->u64)
			ops[op->code - 1].u64 = op->u64;
	}
}


#define NEXT switch ((*ip++).code) \
{ \
	case RET:	goto RET; \
	case BSWAP:	goto BSWAP; \
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
		bswap(&ops[BSWAP - 1], &data[0]);
		NEXT;
}
