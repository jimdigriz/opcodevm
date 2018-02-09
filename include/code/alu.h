typedef enum {
        ADD,
} alu_op_t;

struct opcode_alu {
	unsigned int	c;
	int		k;
	char		*op;
	alu_op_t	opcode;
};
