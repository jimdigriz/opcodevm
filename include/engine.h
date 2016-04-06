#include <stdint.h>
#include <stddef.h>

enum endian {
	LITTLE,
	BIG,
};

enum op {
	RET	= 0,	/* must be set to zero */
	BSWAP,
};

/* programs must terminate with a RET (also a zero'd struct) */
struct program {
	enum op		op;
};

struct data {
	void		*addr;
	uint64_t	numrec;
	unsigned int	reclen;
	enum endian	endian;
	char		*path;
	int		fd;
};

void engine(struct program *, size_t, struct data *);
