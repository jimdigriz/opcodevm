#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <pcap/pcap.h>

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
	INDIRECT,
	BACKED,
	PACKET,
} column_type_t;

struct ring {
	void		*addr;
	unsigned int	blen;
	unsigned int	in, out;
	pthread_mutex_t	lock;
	sem_t		has_data, has_room;
	pthread_t	thread;
	long		pagesize;
};

struct ringblkinfo {
	unsigned int	nrecs;
};

struct column_ctype_indirect {
	column_type_t	type;
};

struct column_ctype_backed {
	struct ring	*ring;
	unsigned int	nrecs;
	endian_t	endian;
	struct stat	stat;
	unsigned int	offset;
	int		fd;
	char		path[MAX_FILEPATH_LENGTH];
};

struct column_ctype_packet {
	pcap_t		*pcap;
	unsigned int	nrecs;
	char		path[MAX_FILEPATH_LENGTH];
};

struct column {
	void		*addr;
	unsigned int	width;
	datatype_t	type;
	column_type_t	ctype;

#	define COLUMN_CTYPE(x) struct column_ctype_##x	x;
	union {
		COLUMN_CTYPE(indirect)
		COLUMN_CTYPE(backed)
		COLUMN_CTYPE(packet)
	};
};

void column_init(struct column *C);
void column_fini(struct column *C);
unsigned int column_get(struct column *C);
void column_put(struct column *C);

#define DISPATCH_INIT_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_FINI_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_GET_PARAMS	struct column *C, const unsigned int i
#define DISPATCH_PUT_PARAMS	struct column *C, const unsigned int i

#define COLUMN_DEF(x)		void x##_init(DISPATCH_INIT_PARAMS);		\
				void x##_fini(DISPATCH_FINI_PARAMS);		\
				unsigned int x##_get(DISPATCH_GET_PARAMS);	\
				void x##_put(DISPATCH_PUT_PARAMS);
COLUMN_DEF(zero)
COLUMN_DEF(indirect)
COLUMN_DEF(backed)
COLUMN_DEF(packet)
