
OS := $(shell uname)

CFLAGS = -Wall -Wextra -Werror
ifeq ($(OS),Darwin)
CFLAGS += -m32
CFLAGS += -mmacosx-version-min=10.6
endif

PROG = sercons

$(PROG): $(PROG).c
	gcc $(CFLAGS) -o $@ $<

clean:
	rm -rf $(PROG) $(PROG).dSYM

