#include <stdint.h>
#include <assert.h>

#include "engine.h"

void bswap(struct op *ops, struct data *data, ...)
{
	uint64_t offset = 0;

	ops->u32(&offset, data);

	assert(offset == data->numrec);
}
