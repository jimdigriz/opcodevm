SRCS		:= $(wildcard *.c)
CODESRCS	:= $(wildcard code/*.c)
CODEOBJS	:= $(CODESRCS:%.c=%.o)
OBJS		:= $(SRCS:%.c=%.o) $(CODEOBJS)

OPSRCS		:= $(foreach s,$(basename $(CODESRCS)),$(wildcard $(s)/*.c))
OPOBJS		:= $(OPSRCS:%.c=%.so)

TARGET		:= opcodevm
TARGETS		:= $(TARGET) $(OPOBJS)

VERSION		:= $(shell git rev-parse --short HEAD)$(shell git diff-files --quiet || printf -- -dirty)

CPPFLAGS	+= -MD -MP -Iinclude -I.
CFLAGS		+= -std=c99 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -fPIC -DVERSION="\"$(VERSION)\""
LDFLAGS		+= -rdynamic -ldl -lpthread

CFLAGS		+= -march=native -mtune=native

ifdef NDEBUG
	CFLAGS	+= -O3 -fstack-protector-strong -DNDEBUG
else
	CFLAGS	+= -Og -g3 -fstack-protector-all -fsanitize=address
	LDFLAGS	+= -fsanitize=address
endif

ifdef PROFILE
	CFLAGS  += -pg -fprofile-generate -ftest-coverage
	LDFLAGS += -pg -lgcov -coverage
endif

# better stripping
CFLAGS	+= -fdata-sections
ifndef PROFILE
	CFLAGS	+= -ffunction-sections
endif
LDFLAGS	+= -Wl,--gc-sections

.SUFFIXES:

all: $(TARGETS)

$(TARGET): $(OBJS) Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -o $@ $(filter %.o, $^)
ifdef NDEBUG
	$(CROSS_COMPILE)strip $@
endif

%.o: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.so: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -shared -nostartfiles -o $@ $<
ifdef NDEBUG
	$(CROSS_COMPILE)strip $@
endif

clean:
	rm -rf $(SRCS:%.c=%.d) $(CODESRCS:%.c=%.d) $(OPSRCS:%.c=%.d) $(TARGETS) $(OBJS)
.PHONY: clean

-include $(SRCS:%.c=%.d)
