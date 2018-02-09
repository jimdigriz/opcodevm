#include <sys/queue.h>
#include <stddef.h>

#include "column.h"

#define OPCODES_MAX		16

#define OPCODE_PARAMS		const struct column *C, const unsigned int n, const void *ops

#define XENGINE_HOOK(x,y,z)	ENGINE_HOOK(x,y,z)
#define ENGINE_HOOK(x,y,z)	engine_opcode_imp_init(#x, &x##_##y##_##z##_imp);

struct opcode {
	SLIST_ENTRY(opcode) opcode;

	int		(*func)(OPCODE_PARAMS);
	void		(*hook)(const void *imp);
	void		(*compile)(void *ops);

	void		(*profile_init)(const unsigned int length, const unsigned int width);
	void		(*profile_fini)();
	void		(*profile)();

	char		*name;
	unsigned int	bytecode;
};

#include "code/ld.h"
#include "code/alu.h"
#include "code/bswap.h"

struct insn {
	ptrdiff_t	code;

	union {
		struct opcode_ld	ld;
		struct opcode_alu	alu;
		struct opcode_bswap	bswap;
	} ops;

	char		*name;
};

struct program {
	struct column	*columns;
	struct insn	*insns;
	unsigned int	len;
};

void engine_opcode_init(struct opcode *opcode);
void engine_opcode_imp_init(const char *name, const void *args);
void engine_init();
void engine_init_columns(struct column *columns);
void engine_fini_columns(struct column *columns);
void engine_run(struct program *program);
