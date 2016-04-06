#include <unistd.h>
#include <err.h>
#include <stdlib.h>

#include "engine.h"
#include "ops.h"

/* http://www.complang.tuwien.ac.at/forth/threading/ : repl-switch */
#define INST(x) case x: goto x
#define NEXT switch ((*ip++).code) \
	{ \
		/* keep in the same order as include/engine.h:op */ \
		INST(RET); \
		INST(BSWAP); \
	}

void engine(struct program *program, size_t proglen, struct data *data)
{
	if (program[proglen - 1].code != RET) {
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

	struct program *ip = program;

	NEXT;

	RET:
		return;
	BSWAP:
		bswap(&data[0]);
		NEXT;
}
