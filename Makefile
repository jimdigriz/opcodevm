SRCS		:= $(wildcard *.c)
OBJS		:= $(SRCS:%.c=%.o)

CODESRCS	:= $(wildcard code/*.c)
CODEOBJS	:= $(CODESRCS:%.c=%.so)

OPSRCS		:= $(foreach s,$(basename $(CODESRCS)),$(wildcard $(s)/*.c))
OPOBJS		:= $(OPSRCS:%.c=%.so)

TARGET		:= opcodevm
TARGETS		:= $(TARGET) $(CODEOBJS) $(OPOBJS)

VERSION		:= $(shell git rev-parse --short HEAD)$(shell git diff-files --quiet || printf -- -dirty)

CPPFLAGS	+= -MD -MP -Iinclude -I.
CFLAGS		+= -std=c11 -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -fPIC -pthread -DVERSION="\"$(VERSION)\""
LDFLAGS		+= -rdynamic -lpthread -pthread -lasan -lm
LDFLAGS_SO	+= $(LDFLAGS) -lOpenCL

CFLAGS		+= -march=native -mtune=native

ifndef NDEBUG
	CFLAGS	+= -O0 -g3 -fstack-protector-all -fsanitize=address
	LDFLAGS	+= -fsanitize=address
	NOSTRIP	:= 1
else
	CFLAGS	+= -O3 -fstack-protector-strong -DNDEBUG
endif

# better stripping
CFLAGS	+= -fdata-sections -ffunction-sections
LDFLAGS	+= -Wl,--gc-sections

.SUFFIXES:

all: $(TARGETS) utils/profile

include/jumptable.h: OPCODES_MAX := $(shell sed -n 's/.*OPCODES_MAX[^0-9]*// p' include/engine.h)
include/jumptable.h: include/engine.h Makefile
	@echo '#pragma GCC diagnostic push'			>  $@
	@echo '#pragma GCC diagnostic ignored "-Wpedantic"'	>> $@
	@echo							>> $@
	@# code '0' is "ret" and hardcoded in engine.c
	@$(foreach i,$(shell seq 1 $$(($(OPCODES_MAX)-1))),printf "bytecode$(i):\n\t\tCALL($(i))\n\t\tNEXT\n" >> $@;)
	@echo							>> $@
	@echo 'static const ptrdiff_t cf[] = {'			>> $@
	@$(foreach i,$(shell seq 0 $$(($(OPCODES_MAX)-1))),printf "\t(uintptr_t)&&bytecode$(i) - (uintptr_t)&&bytecode0,\n" >> $@;)
	@echo '};'						>> $@
	@echo							>> $@
	@echo '#pragma GCC diagnostic pop'			>> $@

engine.o engine.lst: include/jumptable.h Makefile

utils/profile: utils/profile.o engine.o column.o Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -ldl -lm -o $@ $(filter %.o, $^)
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

$(TARGET): $(OBJS) Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -ldl -o $@ $(filter %.o, $^)
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

%.lst: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -c -fverbose-asm -Wa,-adhln $< > $@

%.o: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.so: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS_SO) $(CPPFLAGS) $(CFLAGS) -shared -o $@ $<
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

clean:
	rm -rf $(SRCS:%.c=%.d) $(SRCS:%.c=%.lst) $(CODESRCS:%.c=%.d) $(OPSRCS:%.c=%.d) $(TARGETS) $(OBJS) utils/profile utils/profile.o include/jumptable.h
.PHONY: clean

-include $(SRCS:%.c=%.d)
