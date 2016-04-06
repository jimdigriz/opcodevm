#include <stdint.h>
#include <byteswap.h>
#include <err.h>
#include <assert.h>

#include "engine.h"

void bswap(struct data *data)
{
	uint16_t *d16 = data->addr;
	uint32_t *d32 = data->addr;
	uint64_t *d64 = data->addr;

	uint64_t offset = 0;

	switch (data->reclen) {
	case 2:
		*d16 += offset;
		for (; offset < data->numrec; offset++)
			d16[offset] = bswap_16(d16[offset]);
		break;
	case 4:
		*d32 += offset;
		for (; offset < data->numrec; offset++)
			d32[offset] = bswap_32(d32[offset]);
		break;
	case 8:
		*d64 += offset;
		for (; offset < data->numrec; offset++)
			d64[offset] = bswap_64(d64[offset]);
		break;
	default:
		warnx("unsupported size %u", data->reclen);
	}

	assert(offset = data->reclen);
}
