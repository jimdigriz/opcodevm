#include <stdint.h>
#include <stddef.h>

typedef enum {
	LITTLE,
	BIG,
} endian;

typedef enum {
	RET	= 0,	/* must be set to zero */
	BSWAP,
} code;

/* programs must terminate with a RET (also a zero'd struct) */
struct program {
	code		code;
};

struct data {
	void		*addr;
	uint64_t	numrec;
	unsigned int	reclen;
	endian		endian;
	char		*path;
	int		fd;
};

void engine(struct program *, size_t, struct data *);
