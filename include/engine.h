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
	RET,
	BSWAP,
} code;

#define NCODES 1

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

/*
 * the op struct is walked though, where they are populated via:
 * slot 0: 'accelerated' implementation, otherwise backfilled from slot 1
 * slot 1: always C implementation
 * slot 2: always NULL
 */
#define OP(x) void (*x[3])(uint64_t *, struct data *, ...)
struct op {
	OP(u16);
	OP(u32);
	OP(u64);
	code		code;
};

void engine_init();
void engine_run(struct program *, int ndata, struct data *);

void bswap(struct data *, ...);
struct op* bswap_ops();
