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
CFLAGS		+= -std=gnu11 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -fPIC -pthread -DVERSION="\"$(VERSION)\""
LDFLAGS		+= -rdynamic -lpthread -pthread -lasan -lm
LDFLAGS_SO	+= $(LDFLAGS) -lOpenCL

CFLAGS		+= -march=native -mtune=native

ifdef NDEBUG
	CFLAGS	+= -O3 -fstack-protector-strong -DNDEBUG
else
	CFLAGS	+= -O0 -g3 -fstack-protector-all -fsanitize=address
	LDFLAGS	+= -fsanitize=address
	NOSTRIP	:= 1
endif

# better stripping
CFLAGS	+= -fdata-sections -ffunction-sections
LDFLAGS	+= -Wl,--gc-sections

.SUFFIXES:

all: $(TARGETS) utils/profile

include/jumptable.h: Makefile
	@echo '#pragma GCC diagnostic push'			>  $@
	@echo '#pragma GCC diagnostic ignored "-Wpedantic"'	>> $@
	@echo							>> $@
	@# code '0' is "ret" and hardcoded in engine.c
	@$(foreach i,$(shell seq 1 255),printf "bytecode$(i):\n\t\tCALL($(i))\n\t\tNEXT\n" >> $@;)
	@echo							>> $@
	@echo 'static const ptrdiff_t cf[] = {'			>> $@
	@$(foreach i,$(shell seq 0 255),printf "\t(uintptr_t)&&bytecode$(i) - (uintptr_t)&&bytecode0,\n" >> $@;)
	@echo '};'						>> $@
	@echo							>> $@
	@echo '#pragma GCC diagnostic pop'			>> $@

engine.o: include/jumptable.h Makefile

utils/profile: utils/profile.o engine.o Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -ldl -lm -o $@ $(filter %.o, $^)
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

$(TARGET): $(OBJS) Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -ldl -o $@ $(filter %.o, $^)
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

%.o: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.so: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS_SO) $(CPPFLAGS) $(CFLAGS) -shared -o $@ $<
ifndef NOSTRIP
	$(CROSS_COMPILE)strip $@
endif

clean:
	rm -rf $(SRCS:%.c=%.d) $(CODESRCS:%.c=%.d) $(OPSRCS:%.c=%.d) $(TARGETS) $(OBJS) utils/profile utils/profile.o include/jumptable.h
.PHONY: clean

-include $(SRCS:%.c=%.d)
