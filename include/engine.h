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

/*
 * The instruction encodings (OOOrrCCC)
 */
#define OPCODE(x, y) 	(OC_##x|OC_##y)
/* class */
#define OC_CLASS(code)	((code) & 0x07)
#define OC_LD	0x00
#define OC_ST	0x01
#define OC_ALU	0x02
#define OC_JMP	0x03
#define OC_RET	0x06
#define OC_MISC	0x07

/* LD/ST */
#define OC_MODE(code)	((code) & 0xe0)
#define OC_IMM	0x00
#define OC_MEM	0x20
#define OC_COL	0x40

/* ALU/JMP */
#define OC_OP(code)	((code) & 0xe0)
#define OC_ADD	0x00
#define OC_MUL	0x10
#define OC_DIV	0x20
#define OC_OR	0x30
#define OC_AND	0x40
#define OC_JA	0x00
#define OC_JEQ	0x10
#define OC_JGT	0x20
#define OC_JGE	0x30
#define OC_SRC(code)	((code) & 0x08)
#define OC_K	0x00
#define OC_M	0x00

/* MISC */
#define OC_BSWP	0x00
#define OC_SHFT	0x10

struct insn {
	uint8_t		code;
	int64_t		k;
};

/* programs must terminate with a RET (matches also a zero'd struct) */
struct program {
	struct insn	*insns;
	size_t		len;
	size_t		mwords;
};

struct opcode {
	void (*call)(uint64_t *, struct data *, ...);
	void (*reg)(void (*f)(uint64_t *, struct data *, ...), ...);
};
extern struct opcode opcode[];

void engine_init();
void engine_run(struct program *, int ndata, struct data *);
