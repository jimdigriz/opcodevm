#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>

#include "utils.h"
#include "engine.h"

#define OPCODE ld

static int OPCODE(OPCODE_PARAMS)
{
}

static struct opcode opcode = {
	.func		= OPCODE,
	.name		= XSTR(OPCODE),
};

static void __attribute__((constructor)) init()
{
	engine_opcode_init(&opcode);
}
