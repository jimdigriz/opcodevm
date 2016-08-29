#include <stdlib.h>
#include <err.h>
#include <sysexits.h>
#include <string.h>
#include <errno.h>

#include "global.h"
#include "column.h"

#define CTYPE(x) zero_##x

void CTYPE(init)(DISPATCH_INIT_PARAMS)
{
	if (!C[i].width)
		errx(EX_USAGE, "C[i].width");

	C[i].addr = NULL;
}

void CTYPE(fini)(DISPATCH_FINI_PARAMS)
{
	C[i].addr = NULL;
}

unsigned int CTYPE(get)(DISPATCH_GET_PARAMS)
{
	errno = posix_memalign(&C[i].addr, pagesize, stride * C[i].width / 8);
	if (errno)
		err(EX_OSERR, "posix_memalign()");

	memset(C[i].addr, 0, stride * C[i].width / 8);

	return stride;
}

void CTYPE(put)(DISPATCH_PUT_PARAMS)
{
	free(C[i].addr);

	C[i].addr = NULL;
}
