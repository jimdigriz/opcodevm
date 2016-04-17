#include <stdint.h>
#include <stddef.h>
#include <sys/queue.h>

#include "store.h"

struct data {
	void	*addr;
	size_t	nrec;
	size_t	width;
	type	type;
	char	*path;
	int	fd;
};

#define OPCODE_PARAMS	size_t * offset, const size_t nrec, \
			struct data *D[], \
			const size_t dest, const size_t src1, const size_t src2

struct opcode_imp {
	void	(*func)(size_t *offset, const size_t nrec, void *D);

	size_t	cost;
	size_t	width;

	char	*name;
};

struct opcode {
	SLIST_ENTRY(opcode) opcode;

	void	(*func)(OPCODE_PARAMS);
	void	(*hook)(struct opcode_imp *imp);

	void	(*profile_init)(const size_t, const size_t, const size_t);
	void	(*profile_fini)();
	void	(*profile)();

	char	*name;
};

struct bytecode {
	uint8_t		code;
};

struct insn {
	char		*code;

	size_t		dest;
	size_t		src1;
};

struct routine {
	struct insn	*insns;
	size_t		len;
};

struct program {
	struct routine	*begin;
	struct routine	*loop;
	struct routine	*end;
};

void engine_opcode_init(struct opcode *opcode);
void engine_opcode_imp_init(struct opcode_imp *imp);
void engine_init();
void engine_run(struct program *program, size_t nD, struct data *D);
