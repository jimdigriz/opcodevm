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
CFLAGS		+= -std=c11 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -fPIC -pthread -DVERSION="\"$(VERSION)\""
LDFLAGS		+= -ldl -rdynamic -lpthread -pthread
LDFLAGS_SO	+= $(LDFLAGS) -lOpenCL

CFLAGS		+= -march=native -mtune=native

ifdef NDEBUG
	CFLAGS	+= -O3 -fstack-protector-strong -DNDEBUG
else
	CFLAGS	+= -O0 -g3 -fstack-protector-all -fsanitize=address
	LDFLAGS	+= -fsanitize=address
	NOSTRIP	:= 1
endif

ifdef PROFILE
	CFLAGS	+= -finstrument-functions -finstrument-functions-exclude-file-list=inst.c,/include/
else
	OBJS	:= $(filter-out inst.o,$(OBJS))
endif

# better stripping
CFLAGS	+= -fdata-sections -ffunction-sections
LDFLAGS	+= -Wl,--gc-sections

.SUFFIXES:

all: $(TARGETS)

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
	rm -rf $(SRCS:%.c=%.d) $(CODESRCS:%.c=%.d) $(OPSRCS:%.c=%.d) $(TARGETS) $(OBJS)
.PHONY: clean

-include $(SRCS:%.c=%.d)
