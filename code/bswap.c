#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>
#include <pthread.h>

#include "common.h"
#include "engine.h"

#define OPCODE bswap

static endian_t endian
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	= LITTLE;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	= BIG;
#else
#error unknown host endian
#endif

#define SLOTS 5
#define WIDTH2POS(x) ((int)(x/16-1))

struct dispatch {
	void		(*func)(OPCODE_IMP_BSWAP_PARAMS);
	unsigned int	cost;
};
static struct dispatch D[SLOTS*WIDTH2POS(64) + SLOTS];

static int OPCODE(OPCODE_PARAMS)
{
	const struct opcode_bswap *p = ops;

	assert(WIDTH2POS(C[p->dest].width) >= WIDTH2POS(16) && WIDTH2POS(C[p->dest].width) <= WIDTH2POS(64));

	if (C[p->dest].ctype == BACKED) {
		if (p->target == HOST && C[p->dest].backed.endian == endian)
			goto exit;

		if (p->target == C[p->dest].backed.endian)
			goto exit;
	}

	unsigned int slot = SLOTS * WIDTH2POS(C[p->dest].width);
	unsigned int o = 0;
	while (o < n)
		if (n - o >= D[slot].cost)
			D[slot++].func(&C[p->dest], n, &o);

exit:
	return 1;
}

static void hook(const void *args)
{
	const struct opcode_imp_bswap *imp = args;

	assert(WIDTH2POS(imp->width) >= WIDTH2POS(16) && WIDTH2POS(imp->width) <= WIDTH2POS(64));

	void (*func)(OPCODE_IMP_BSWAP_PARAMS) = imp->func;
	unsigned int cost = imp->cost;

	void (*ofunc)(OPCODE_IMP_BSWAP_PARAMS);
	unsigned int ocost;

	const unsigned int offset = SLOTS * WIDTH2POS(imp->width);
	/* last slot should be null */
	for (unsigned int i = offset; i < offset + SLOTS - 1; i++) {
		if (!D[i].func) {
			D[i].func = func;			D[i].cost = cost;
			break;
		}
		else if (D[i].cost < cost) {
			ofunc = D[i].func;
			D[i].func = func;
			func = ofunc;

			ocost = D[i].cost;
			D[i].cost = cost;
			cost = ocost;
		}
	}

	assert(!D[offset + SLOTS - 1].func);
}

static struct column *C;
static unsigned int nrecs;

static void profile_init(const unsigned int length, const unsigned int width)
{
	assert(width > 0);
	assert(length % width == 0);

	C = calloc(2, sizeof(struct column));
	if (!C)
		err(EX_OSERR, "calloc()");

	C[0].ctype = ZERO;
	C[0].width = width;

	C[1].ctype = VOID;

	column_init(C);
	nrecs = column_get(C);
}

static void profile_fini()
{
	column_put(C);
	column_fini(C);
	nrecs = 0;
	free(C);
}

static void profile()
{
	struct opcode_bswap ops = {
		.dest	= 0,
		.target	= HOST,
	};
	OPCODE(C, nrecs, &ops);
}

static struct opcode opcode = {
	.func		= OPCODE,
	.hook		= hook,
	.profile_init	= profile_init,
	.profile_fini	= profile_fini,
	.profile	= profile,
	.name		= XSTR(OPCODE),
};

static void __attribute__((constructor)) init()
{
	engine_opcode_init(&opcode);
}
