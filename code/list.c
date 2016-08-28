#include <stdio.h>
#include <err.h>
#include <sysexits.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>

#include "utils.h"
#include "engine.h"

#define OPCODE list

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int OPCODE(OPCODE_PARAMS)
{
	(void)ops;

	if (getenv("NODISP"))
		goto exit;

	errno = pthread_mutex_lock(&lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_lock(list)");

	for (unsigned int o = 0; o < n; o++) {
		for (unsigned int i = 0; C[i].ctype != VOID; i++) {
			char *comma = (i == 0) ? "" : ",";

			switch (C[i].type) {
			case FLOAT:
#define CASE_FLOAT(x, y)	case x: printf("%s%f", comma, ((y *)C[i].addr)[o]); break;
				switch (C[i].width) {
				CASE_FLOAT(32, float)
				CASE_FLOAT(64, double)
				default:
					errx(EX_SOFTWARE, "FLOAT%d\n", C[i].width);
				}
				break;
			case SIGNED:
#define CASE_SIGNED(x)		case x: printf("%s%" PRIi##x, comma, ((int##x##_t *)C[i].addr)[o]); break;
				switch (C[i].width) {
				CASE_SIGNED(8)
				CASE_SIGNED(16)
				CASE_SIGNED(32)
				CASE_SIGNED(64)
				default:
					errx(EX_SOFTWARE, "SIGNED%d\n", C[i].width);
				}
				break;
			case UNSIGNED:
#define CASE_UNSIGNED(x)	case x: printf("%s%" PRIu##x, comma, ((uint##x##_t *)C[i].addr)[o]); break;
				switch (C[i].width) {
				CASE_UNSIGNED(8)
				CASE_UNSIGNED(16)
				CASE_UNSIGNED(32)
				CASE_UNSIGNED(64)
				default:
					errx(EX_SOFTWARE, "UNSIGNED%d\n", C[i].width);
				}
				break;
			default:
				errx(EX_SOFTWARE, "no idea what to do with type\n");
			}
		}

		printf("\n");
	}

	errno = pthread_mutex_unlock(&lock);
	if (errno)
		err(EX_OSERR, "pthread_mutex_unlock(list)");

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
