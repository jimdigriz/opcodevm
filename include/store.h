#define MAGIC 0x561b13d6f3

typedef enum {
	INT	= 0,
	UINT	= 1,
	FLOAT	= 2,
	CHAR	= 3,
} type;

typedef enum {
	B16	= 4,
	B32	= 5,
	B64	= 6,
	B128	= 7,
} pow2;

#define POW2BYTES(x) ((1 << x)/8)

struct store {
	uint32_t	magic;
	uint8_t		version;

	union {
		char	pad[4096 - sizeof(uint32_t) - sizeof(uint8_t)];

		struct {
			uint8_t	type;
			uint8_t	pow2;
		} v0;
	};
};
