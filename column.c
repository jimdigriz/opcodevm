#include <assert.h>
#include <limits.h>

#include "column.h"

struct dispatch {
	void		(*init)(DISPATCH_INIT_PARAMS);
	void		(*fini)(DISPATCH_FINI_PARAMS);
	unsigned int	(*get)(DISPATCH_GET_PARAMS);
	void		(*put)(DISPATCH_PUT_PARAMS);
};

static struct dispatch dispatch[] = {
	[BACKED]	= {
		.init	= backed_init,
		.fini	= backed_fini,
		.get	= backed_get,
		.put	= backed_put,
	},
};

void column_init(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].init(C, i);
}

void column_fini(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].fini(C, i);
}

unsigned int column_get(struct column *C)
{
	unsigned int nrecs = UINT_MAX;

	for (unsigned int i = 0; C[i].ctype != VOID; i++) {
		unsigned int n = dispatch[C[i].ctype].get(C, i);
		if (n < nrecs)
			nrecs = n;
	}

	assert(nrecs != UINT_MAX);

	return nrecs;
}

void column_put(struct column *C)
{
	for (unsigned int i = 0; C[i].ctype != VOID; i++)
		dispatch[C[i].ctype].put(C, i);
}
