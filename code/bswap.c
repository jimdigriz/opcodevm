#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>
#include <string.h>

#include "engine.h"

#define OPCODE bswap

void (*bswap16)(size_t *offset, const size_t nrec, void *D);
void (*bswap32)(size_t *offset, const size_t nrec, void *D);
void (*bswap64)(size_t *offset, const size_t nrec, void *D);

static void OPCODE(OPCODE_PARAMS)
{
	(void)src1;
	(void)src2;

#	define	JUMP(x)	case (x/8): bswap##x(offset, nrec, D[dest]->addr); break;

	switch (D[dest]->width) {
	JUMP(16)
	JUMP(32)
	JUMP(64)
	default:
		errx(EX_SOFTWARE, "unable to handle width %zu", D[dest]->width);
	}
}

static void hook(struct opcode_imp *imp)
{
#	define HOOK(x) case x: bswap##x = imp->func; break;

	switch (imp->width) {
	HOOK(16)
	HOOK(32)
	HOOK(64)
	default:
		errx(EX_SOFTWARE, "unable to handle width %zu", imp->width);
	}
}

static void *profD;
static size_t profW;

static void profile_init(const size_t length, const size_t alignment, const size_t width)
{
	assert(length % width == 0);
	assert(alignment > 0);
	assert(width > 0);

	profD = aligned_alloc(alignment, length);
	if (!profD)
		err(EX_OSERR, "aligned_alloc()");

	profW = width;

	memset(profD, 69, profW);
}

static void profile_fini()
{
	free(profD);

	profD = NULL;
	profW = 0;
}

static void profile()
{
}

static struct opcode opcode = {
	.func		= OPCODE,
	.hook		= hook,
	.profile_init	= profile_init,
	.profile_fini	= profile_fini,
	.profile	= profile,
	.name		= "bswap",
};

static void __attribute__((constructor)) init()
{
	engine_opcode_init(&opcode);
}
