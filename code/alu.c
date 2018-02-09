#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <sysexits.h>

#include "utils.h"
#include "engine.h"

#define OPCODE alu

struct opcode_alu opcode_alu_map[] = {
	{
		.op	= "add",
		.opcode	= ADD,
	},
};

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
