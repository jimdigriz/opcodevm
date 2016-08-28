#include <stdlib.h>
#include <err.h>
#include <sysexits.h>

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
	C[i].addr = calloc(stride, C[i].width/8);
	if (!C[i].addr)
		err(EX_OSERR, "calloc()");

	return stride;
}

void CTYPE(put)(DISPATCH_PUT_PARAMS)
{
	free(C[i].addr);

	C[i].addr = NULL;
}
