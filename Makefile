# Makefile -- MM-B1 business RPN calculator.
#
# Decimal arithmetic is the vendored IBM decNumber reference library
# (vendor/decNumber), so there is no external decimal dependency.  The only
# external library is GNU readline.
#
#   make            build ./mmb1
#   make run        build and run interactively
#   make test       build and run the regression script
#   make clean
#
# macOS: install readline via Homebrew and point the build at it:
#   brew install readline
#   make READLINE_PREFIX="$(brew --prefix readline)"

CC      ?= cc
CSTD    ?= -std=c11
OPT     ?= -O2
WARN    ?= -Wall -Wextra
DECDEF   = -DHAVE_STDINT_H -DHAVE_STDBOOL_H
INCLUDE  = -Ivendor/decNumber

# GNU readline. On macOS pass READLINE_PREFIX=$(brew --prefix readline).
READLINE_PREFIX ?=
ifneq ($(READLINE_PREFIX),)
  INCLUDE += -I$(READLINE_PREFIX)/include
  LDFLAGS += -L$(READLINE_PREFIX)/lib
endif
LDLIBS  += -lreadline

CFLAGS  = $(CSTD) $(OPT) $(WARN) $(DECDEF) $(INCLUDE)

DEC_SRC = vendor/decNumber/decNumber.c vendor/decNumber/decContext.c
SRC     = main.c dec.c stack.c finance.c dates.c $(DEC_SRC)
OBJ     = $(SRC:.c=.o)
BIN     = mmb1

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

test: $(BIN)
	@./$(BIN) < tests.mmb

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: run test clean
