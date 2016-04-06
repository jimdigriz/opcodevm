SRCS		:= $(wildcard *.c) $(wildcard ops/*.c)
OBJS		:= $(SRCS:%.c=%.o)

TARGET		:= opcodevm

VERSION		:= $(shell git rev-parse --short HEAD)$(shell git diff-files --quiet || printf -- -dirty)

CPPFLAGS	+= -Iinclude
CFLAGS		+= -std=c99 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -fPIC -MD -MP -DVERSION="\"$(VERSION)\""
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

$(TARGET): $(OBJS)

%: %.o Makefile
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -o $@ $(filter %.o, $^)
ifdef NDEBUG
	$(CROSS_COMPILE)strip $@
endif

%.o: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.so: %.c Makefile
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(CPPFLAGS) -shared -nostartfiles -o $@ $<

clean:
	rm -rf $(SRCS:%.c=%.d) $(TARGET) $(OBJS)
.PHONY: clean

-include $(SRCS:%.c=%.d)
