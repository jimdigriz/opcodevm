#include <sys/queue.h>
#include <err.h>
#include <sysexits.h>
#include <assert.h>
#include <string.h>

#include "engine.h"

extern SLIST_HEAD(opcode_list, opcode) opcode_list;

void engine_opcode_init(struct opcode *opcode)
{
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode)
		if (!strcmp(opcode->name, np->name))
			errx(EX_SOFTWARE, "duplicate %s opcode calling init()", opcode->name);
	SLIST_INSERT_HEAD(&opcode_list, opcode, opcode);
}

void engine_opcode_imp_init(struct opcode_imp *opcode_imp)
{
	struct opcode *np;
	SLIST_FOREACH(np, &opcode_list, opcode) {
		if (strcmp(opcode_imp->name, np->name))
			continue;

		np->hook(opcode_imp);
		return;
	}
	errx(EX_SOFTWARE, "missing %s opcode", opcode_imp->name);
}
