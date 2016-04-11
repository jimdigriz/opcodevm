#include <stdint.h>
#include <byteswap.h>
#include <assert.h>

#include "engine.h"

#define FUNC(x)	static void bswap_##x##_c(uint64_t *offset, struct data *data, ...)	\
		{									\
			uint##x##_t *d = data->addr;					\
											\
			for (; *offset < data->numrec; (*offset)++)			\
				d[*offset] = bswap_##x(d[*offset]);			\
		}

FUNC(16)
FUNC(32)
FUNC(64)

static void __attribute__ ((constructor)) init()
{
	assert(opcode[OPCODE(MISC, BSWP)].reg != NULL);

#	define REG(x)	opcode[OPCODE(MISC, BSWP)].reg(bswap_##x##_c, x/8);
	REG(16);
	REG(32);
	REG(64);
}
