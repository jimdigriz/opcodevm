struct opcode_bswap {
	unsigned int	dest;
	endian_t	target;
};

#define OPCODE_IMP_BSWAP_PARAMS	const struct column *C, const unsigned int n, unsigned int * const o

struct opcode_imp_bswap {
	void		(*func)(OPCODE_IMP_BSWAP_PARAMS);
	unsigned int	cost;
	unsigned int	width;
};
