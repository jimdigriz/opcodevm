#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sysexits.h>
#include <err.h>
#include <string.h>
#include <assert.h>

#include "engine.h"

#define	RECLEN2POS(x)	((int)(x/2-1))
#define MAX_SLOTS	3

/* +1 as we need a NULL terminator on the end of the list */
void (*op[RECLEN2POS(16)][MAX_SLOTS + 1])(uint64_t *, struct data *, ...);

static void call(uint64_t *offset, struct data *data, ...)
{
	va_list ap;
	va_start(ap, data);

	uint64_t n = va_arg(ap, uint64_t);

	va_end(ap);

	assert(RECLEN2POS(data[n].reclen) > -1 && RECLEN2POS(data[n].reclen) < RECLEN2POS(16));

	int i = 0;
	while (*offset < data[n].numrec && op[RECLEN2POS(data[n].reclen)][i])
		op[RECLEN2POS(data[n].reclen)][i++](offset, &data[n]);
}

static void reg(void (*f)(uint64_t *, struct data *, ...), ...)
{
	va_list ap;
	va_start(ap, f);

	unsigned int reclen = va_arg(ap, unsigned int);
	assert(RECLEN2POS(reclen) > -1 && RECLEN2POS(reclen) < RECLEN2POS(16));

	va_end(ap);

	/* +1 is catch by the assert later */
	for (unsigned int i = 0; i < MAX_SLOTS + 1; i++) {
		if (!op[RECLEN2POS(reclen)][i]) {
			op[RECLEN2POS(reclen)][i] = f;
			break;
		}
	}

	assert(op[RECLEN2POS(reclen)][MAX_SLOTS] == NULL);
}

static void __attribute__ ((constructor)) init()
{
	opcode[OPCODE(MISC, BSWP)].call	= call;
	opcode[OPCODE(MISC, BSWP)].reg	= reg;
}
