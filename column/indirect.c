#include <stdlib.h>
#include <err.h>
#include <sysexits.h>
#include <string.h>
#include <errno.h>

#include "global.h"
#include "column.h"

#define CTYPE(x) indirect_##x

void CTYPE(init)(DISPATCH_INIT_PARAMS)
{
	zero_init(C, i);
}

void CTYPE(fini)(DISPATCH_FINI_PARAMS)
{
	zero_fini(C, i);
}

unsigned int CTYPE(get)(DISPATCH_GET_PARAMS)
{
	return zero_get(C, i);
}

void CTYPE(put)(DISPATCH_PUT_PARAMS)
{
	zero_put(C, i);
}
