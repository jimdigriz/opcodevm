SRCS		:= $(wildcard *.c) $(wildcard ops/*.c)
OBJS		:= $(SRCS:%.c=%.o)

TARGET		:= opcodevm

VERSION		:= $(shell git rev-parse --short HEAD)$(shell git diff-files --quiet || printf -- -dirty)

CPPFLAGS	+= -Iinclude
CFLAGS		+= -std=c99 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -fPIC
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

# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
CFLAGS += -MT $@ -MMD -MP -MF $*.Td
POSTCOMPILE = mv -f $*.Td $*.d

.SUFFIXES:

$(TARGET): $(OBJS)

%: %.o
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -o $@ $^
ifdef NDEBUG
	$(CROSS_COMPILE)strip $@
endif

%.o: %.c Makefile %.d
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(CPPFLAGS) -DVERSION="\"$(VERSION)\"" -fopt-info-all=$(<:%.c=%).mopt -c -o $@ $<
	@test -s $(<:%.c=%).mopt || rm $(<:%.c=%).mopt
	$(POSTCOMPILE)

%.so: %.c Makefile %.d
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(CPPFLAGS) -DVERSION="\"$(VERSION)\"" -fopt-info-all=$(<:%.c=%).mopt -shared -nostartfiles -o $@ $<
	@test -s $(<:%.c=%).mopt || rm $(<:%.c=%).mopt
	$(POSTCOMPILE)

clean:
	rm -rf $(TARGET) $(OBJS) $(JETS:%.c=%.so) *.d *.mopt
.PHONY: clean

%.d: ;
.PRECIOUS: %.d

-include $(patsubst %,%.d,$(basename $(SRCS)))
