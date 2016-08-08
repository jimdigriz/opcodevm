#include "common.h"
#include "engine.h"

static struct column columns[] = {
	{
		.ctype	= BACKED,
		.width	= 32,
		.type	= FLOAT,
		.backed	= {
			.path	= "store/test",
			.endian	= BIG,
		},
	},
	{
		.ctype	= CAST,
		.width	= 16,
		.type	= UNSIGNED,
		.cast	= {
			.src	= 0,
			.offset	= 16,
			.shift	= 16,
			.mask	= 0xffff,
		},
	},
};

static struct insn insns[] = {
	{
		.name		= "bswap",
		.ops.bswap	= {
			.dest	= 0,
			.target	= HOST,
		},
	},
	{
		.name		= "list",
	},
	{
		.name		= "ret",
	},
};

static struct program program = {
	.columns	= columns,
	.ncols		= ARRAY_SIZE(columns),
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
