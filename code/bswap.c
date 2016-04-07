#include <stdint.h>
#include <assert.h>

#include "engine.h"

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

static struct op ops = {
	.code	= BSWAP,
};

#define C(x) case (x/8): 					\
		while (offset < data->numrec && ops.u##x[i])	\
			ops.u##x[i++](&offset, data);		\
		break;

void bswap(struct data *data, ...)
{
	uint64_t offset = 0;
	int i = 0;

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
