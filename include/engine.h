#include <stdint.h>
#include <stddef.h>

#include <store.h>

struct data {
	void		*addr;
	uint64_t	numrec;
	type		type;
	uint8_t		reclen;
	char		*path;
	int		fd;
};

typedef enum {
	BSWAP,
	CODE_MAX,
	RET,		/* after CODE_MAX as this has no ops */
} code;

struct insn {
	code		code;
	int64_t		k;
};

/* programs must terminate with a RET (matches also a zero'd struct) */
struct program {
	struct insn	*insns;
	size_t		len;
	size_t		rwords;
};

struct opcode {
	void (*call)(uint64_t *, struct data *, ...);
	void (*reg)(void (*f)(uint64_t *, struct data *, ...), ...);
};
extern struct opcode opcode[];

void engine_init();
void engine_run(struct program *, int ndata, struct data *);
