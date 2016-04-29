#include <stdio.h>
#include <err.h>
#include <sysexits.h>
#include <stdlib.h>

#include "common.h"
#include "engine.h"

#define OPCODE list

static int OPCODE(OPCODE_PARAMS)
{
	(void)ops;

	if (getenv("NODISP"))
		goto exit;

	for (unsigned int o = 0; o < n; o++) {
		for (unsigned int i = 0; C[i].addr; i++) {
			char *comma = (i == 0) ? "" : ",";

			switch (C[i].type) {
			case FLOAT:
				switch (C[i].width) {
				case 32:
					printf("%s%f", comma, ((float *)C[i].addr)[o]);
					break;
				case 64:
					printf("%s%f", comma, ((double *)C[i].addr)[o]);
					break;
				default:
					errx(EX_SOFTWARE, "FLOAT%d\n", C[i].width);
				}
				break;
			default:
				errx(EX_SOFTWARE, "no idea what to do with type\n");
			}
		}

		printf("\n");
	}

exit:
	return 1;
}

static struct opcode opcode = {
	.func		= OPCODE,
	.name		= XSTR(OPCODE),
};

static void __attribute__((constructor)) init()
{
	engine_opcode_init(&opcode);
}
