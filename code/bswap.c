#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <string.h>

#include "engine.h"
#include "engine-hooks.h"

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

static struct data *D;

static void profile_init(const size_t length, const size_t alignment, const size_t width)
{
	assert(alignment == 4096);
	assert(width > 0);
	assert(alignment % width == 0);
	assert(length % width == 0);

	D = calloc(1, sizeof(struct data));
	if (!D)
		err(EX_OSERR, "calloc()");

	D[0].addr = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (!D[0].addr)
		err(EX_OSERR, "mmap()");

	D[0].width	= width;
	D[0].nrec	= length / width;
}

static void profile_fini()
{
	munmap(D[0].addr, D[0].width * D[0].nrec);
	free(D);
}

static void profile()
{
	size_t offset = 0;
	OPCODE(&offset, D[0].nrec, &D, 0, 0, 0);
	assert(offset == D[0].nrec);
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
