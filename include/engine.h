#include <sys/queue.h>
#include <stddef.h>

#define OPCODE_PARAMS		const struct column *C, unsigned int o, const unsigned int e, const void *ops
#define MAX_FILEPATH_LENGTH	1000

#define XENGINE_HOOK(x,y,z)	ENGINE_HOOK(x,y,z)
#define ENGINE_HOOK(x,y,z)	engine_opcode_imp_init(#x, &x##_##y##_##z##_imp);

typedef enum {
	BITARRAY,
	FLOAT,
	SIGNED,
	UNSIGNED,
} datatype_t;

typedef enum {
	HOST,
	LITTLE,
	BIG,
} endian_t;

typedef enum {
	VOID	= 0,
	MEMORY,
	MAPPED,
	CAST,
} column_type_t;

struct column {
	void		*addr;
	unsigned int	width;
	datatype_t	type;
	unsigned int	nrecs;
	column_type_t	ctype;
	union {
		struct {
			endian_t	endian;
			char		path[MAX_FILEPATH_LENGTH];
			unsigned int	offset;
		} mapped;
		struct {
			unsigned int	src;
			unsigned int	offset;
			unsigned int	stride;
			int		shift;
			unsigned int	mask;
		} cast;
	};
};

struct opcode {
	SLIST_ENTRY(opcode) opcode;

	int		(*func)(OPCODE_PARAMS);
	void		(*hook)(const void *imp);

	void		(*profile_init)(const unsigned int length, const unsigned int width);
	void		(*profile_fini)();
	void		(*profile)();

	char		*name;
	unsigned int	bytecode;
};

#include "code/bswap.h"

struct insn {
	ptrdiff_t	code;

	union {
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
