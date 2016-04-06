#include <unistd.h>
#include <err.h>
#include <stdlib.h>
#include <assert.h>

#include "engine.h"
#include "ops.h"

/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
#define NEXT switch ((*ip++).op) \
	{ \
		/* keep in the same order as include/engine.h:'enum op' */ \
		case RET:	goto RET; \
		case BSWAP:	goto BSWAP; \
	}

void engine(struct program *program, size_t proglen, struct data *data)
{
#ifndef NDEBUG
	assert(program[proglen - 1].op == RET);

	for (int i = 0; data[i].addr; i++) {
		if ((uintptr_t)data[i].addr % getpagesize() != 0) {
			warnx("data[%d] is not page aligned\n", i);
			abort();
		}
	}
#endif

	struct program *ip = program;

	NEXT;

	RET:
		return;
	BSWAP:
		bswap(&data[0]);
		NEXT;
}
