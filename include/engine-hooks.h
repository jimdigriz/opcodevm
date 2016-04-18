void engine_opcode_init(struct opcode *opcode);
void engine_opcode_imp_init(struct opcode_imp *imp);

void engine_opcode_map(void (*opcode[256])(OPCODE_PARAMS));
