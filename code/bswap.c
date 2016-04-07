#include <stdint.h>
#include <assert.h>

#include "engine.h"

void bswap(struct op *ops, struct data *data, ...)
{
	uint64_t offset = 0;

	switch (data->reclen) {
	case 2:
		ops->u16(&offset, data);
		break;
	case 4:
		ops->u32(&offset, data);
		break;
	case 8:
		ops->u64(&offset, data);
		break;
	}

//	assert(offset == data->numrec);
}
