#include "utils.h"
#include "engine.h"

static struct column columns[] = {
#if 0
	{
		.ctype	= BACKED,
		.width	= 32,
		.type	= FLOAT,
		.backed	= {
			.path	= "store/test",
			.endian	= BIG,
		},
	},
#endif
	{
		.ctype	= PACKET,
		.width	= 2048 * 8,
		.packet	= {
			.path	= "store/test.pcap",
		},
	},
	{
		.ctype	= INDIRECT,
	},
	{
		.ctype	= VOID,
	}
};

static struct insn insns[] = {
#if 0
	{
		.name		= "bswap",
		.ops.bswap	= {
			.dst	= 0,
			.target	= HOST,
		},
	},
	{
		.name		= "ld",
		.ops.ld		= {
			.c	= 1,	// if points to indirect type, then k is column source
			.k	= 0,
		},
	},
#endif
	{
		.name		= "alu",
		.ops.alu	= {
			.c	= 0,
			.k	= 2,
			.op	= "add",
		},
	},
	{
		.name		= "list",
	},
	{
		.name		= "ret",
	}
};

static struct program program = {
	.columns	= columns,
	.insns		= insns,
	.len		= ARRAY_SIZE(insns),
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	engine_init();
	engine_run(&program);

	return 0;
}
