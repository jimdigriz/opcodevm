#include <stdint.h>
#include <byteswap.h>

#include "engine.h"

#define FUNC(x)	static void bswap##x(uint64_t *offset, struct data *data, ...)		\
		{									\
			uint##x##_t *d = data->addr;					\
											\
			*d += *offset;							\
			for (; *offset < data->numrec; (*offset)++)			\
				d[*offset] = bswap_##x(d[*offset]);			\
		}

FUNC(16)
FUNC(32)
FUNC(64)

#define F(x) .u##x = bswap##x
static struct op op = {
	.code = BSWAP,
	F(16),
	F(32),
	F(64),
};

struct op* init()
{
	return &op;
}
