#include <sys/stat.h>
#include <pthread.h>

#define MAX_FILEPATH_LENGTH	1000

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
	ZERO,
	BACKED,
	CAST,
	PACKET,
} column_type_t;

struct column_ctype_backed {
	unsigned int	nrecs;
	endian_t	endian;
	struct stat	stat;
	unsigned int	offset;
	int		fd;
	char		path[MAX_FILEPATH_LENGTH];
};

struct column_ctype_cast {
	unsigned int	src;
	unsigned int	offset;
	int		shift;
	unsigned int	mask;
};

struct column_ctype_packet {
	int		sock;
};

struct column {
	void		*addr;
	unsigned int	width;
	unsigned int	nrecs;
	datatype_t	type;
	column_type_t	ctype;

#	define COLUMN_CTYPE(x) struct column_ctype_##x	x;
	union {
		COLUMN_CTYPE(backed)
		COLUMN_CTYPE(cast)
		COLUMN_CTYPE(packet)
	};
};

void column_init(struct column *C);
void column_fini(struct column *C);
unsigned int column_get(struct column *C, const unsigned int s, unsigned int * const offset, pthread_mutex_t * const offsetlk);
void column_put(struct column *C);
