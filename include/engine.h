#include <stdint.h>
#include <stddef.h>

typedef enum {
	LITTLE,
	BIG,
} endian;

struct data {
	void		*addr;
	uint64_t	numrec;
	unsigned int	reclen;
	endian		endian;
	char		*path;
	int		fd;
};

typedef enum {
	RET	= 0,	/* RET must be zero */
	BSWAP,
} code;

#define NCODES 1

struct insn {
	code		code;
};

/* programs must terminate with a RET (matches also a zero'd struct) */
struct program {
	struct insn	*insns;
	size_t		len;
};

#define OP(x) void (*x)(uint64_t *, struct data *, ...)
struct op {
	OP(u16);
	OP(u32);
	OP(u64);
	code		code;
};

void engine_init();
void engine_run(struct program *, struct data *);

void bswap(struct op *, struct data *, ...);
