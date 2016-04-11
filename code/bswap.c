#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

#include "engine.h"

static struct op ops = {
	.code	= BSWAP,
};

void bswap(struct data *data, ...)
{
	va_list ap;
	va_start(ap, data);

	uint64_t n = va_arg(ap, uint64_t);

	va_end(ap);

	uint64_t offset = 0;
	int i = 0;

#	define C(x) 	case (x/8):	 				\
			while (offset < data->numrec && ops.u##x[i])	\
				ops.u##x[i++](&offset, &data[n]);	\
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
