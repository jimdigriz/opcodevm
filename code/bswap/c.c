#include <stdint.h>
#include <byteswap.h>

#include "engine.h"
#include "engine-hooks.h"

#define OPCODE	bswap
#define IMP	c

#define FUNC(x) static void bswap_##x##_c(size_t *offset, const size_t nrec, void *D)	\
		{									\
			uint##x##_t *d = D;						\
											\
			for (; *offset < nrec; (*offset)++)				\
				d[*offset] = bswap_##x(d[*offset]);			\
		}									\
		static struct opcode_imp bswap##_##x##_c_imp = {			\
			.func	= bswap##_##x##_c,					\
			.cost	= 0,							\
			.width	= x,							\
			.name	= "bswap",						\
		};

FUNC(16)
FUNC(32)
FUNC(64)

static void __attribute__((constructor)) init()
{
#	define HOOK(x)	engine_opcode_imp_init(&bswap##_##x##_c_imp)
	HOOK(16);
	HOOK(32);
	HOOK(64);
}
