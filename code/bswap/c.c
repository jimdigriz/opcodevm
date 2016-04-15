#include <stdint.h>
#include <byteswap.h>
#include <assert.h>

#include "engine.h"
#include "inst.h"

#define FUNC(x)	PERF_STORE(bswap_##x##_c)						\
		static void bswap_##x##_c(uint64_t *offset, struct data *data, ...)	\
		{									\
			uint##x##_t *d = data->addr;					\
											\
			PERF_INIT(bswap_##x##_c, PERF_CYCLES);				\
			PERF_UNPAUSE(bswap_##x##_c, *offset);				\
											\
			for (; *offset < data->numrec; (*offset)++)			\
				d[*offset] = bswap_##x(d[*offset]);			\
											\
			PERF_PAUSE(bswap_##x##_c);					\
			PERF_MEASURE(bswap_##x##_c, *offset);				\
		}

FUNC(16)
FUNC(32)
FUNC(64)

static void __attribute__((constructor)) init()
{
	assert(opcode[OPCODE(MISC, BSWP)].reg != NULL);

#	define REG(x)	opcode[OPCODE(MISC, BSWP)].reg(bswap_##x##_c, x/8);
	REG(16);
	REG(32);
	REG(64);
}
