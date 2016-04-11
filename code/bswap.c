#include <stdint.h>
#include <assert.h>

#include "engine.h"

static struct op ops = {
	.code	= BSWAP,
};

void bswap(struct data *data, ...)
{
	uint64_t offset = 0;
	int i = 0;

#	define C(x) 	case (x/8):	 				\
			while (offset < data->numrec && ops.u##x[i])	\
				ops.u##x[i++](&offset, data);		\
			break;

	switch (data->reclen) {
		C(16);
		C(32);
		C(64);
	}

	assert(offset == data->numrec);
}

struct op* bswap_ops()
{
	return &ops;
}
