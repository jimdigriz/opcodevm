SRCS		:= $(wildcard *.c)
OBJS		:= $(SRCS:%.c=%.o)

TARGET		:= opcodevm

VERSION		:= $(shell git rev-parse --short HEAD)$(shell git diff-files --quiet || printf -- -dirty)

CPPFLAGS	+= -Iinclude
CFLAGS		+= -std=c99 -pedantic -pedantic-errors -Wall -Wextra -Wcast-align -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L
LDFLAGS		+= -lpthread

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

# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
CFLAGS += -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(TARGET): Makefile $(OBJS)

%: %.o
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -o $@ $<
ifdef NDEBUG
	$(CROSS_COMPILE)strip $@
endif

%.o: %.c Makefile $(DEPDIR)/%.d
	$(CROSS_COMPILE)$(CC) -c $(CFLAGS) $(CPPFLAGS) -fopt-info-all=$(<:%.c=%).mopt -DVERSION="\"$(VERSION)\"" -o $@ $<
	@test -s $(<:%.c=%).mopt || rm $(<:%.c=%).mopt
	$(POSTCOMPILE)

clean:
	rm -rf $(TARGET) $(OBJS) $(DEPDIR) *.mopt
.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS)))
