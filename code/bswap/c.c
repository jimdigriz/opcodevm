#include <stdint.h>
#include <byteswap.h>

#include "engine.h"

#define OPCODE	bswap
#define IMP	c

#define XFUNC(x,y,z)	FUNC(x,y,z)
#define FUNC(x,y,z)	static void x##_##y##_##z(OPCODE_IMP_BSWAP_PARAMS)	\
			{							\
				uint##y##_t *d = C->addr;			\
										\
				for (; *o < n; (*o)++)				\
					d[*o] = bswap_##y(d[*o]);		\
			}							\
			static struct opcode_imp_##x x##_##y##_##z##_imp = {	\
				.func	= x##_##y##_##z,			\
				.cost	= 0,					\
				.width	= y,					\
			};

XFUNC(OPCODE, 16, IMP)
XFUNC(OPCODE, 32, IMP)
XFUNC(OPCODE, 64, IMP)

static void __attribute__((constructor)) init()
{
	XENGINE_HOOK(OPCODE, 16, IMP)
	XENGINE_HOOK(OPCODE, 32, IMP)
	XENGINE_HOOK(OPCODE, 64, IMP)
}
