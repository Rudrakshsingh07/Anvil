# Anvil: C11 + POSIX editor.  The Python visualizer lives in dsa_visualizer/.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -pthread

SRC := \
	src/util.c \
	src/buffer.c \
	src/undo.c \
	src/clipboard.c \
	src/trie.c \
	src/keywords.c \
	src/fileio.c \
	src/highlight.c \
	src/bridge.c \
	src/editor.c \
	src/ui.c \
	src/main.c

OBJ := $(SRC:src/%.c=build/%.o)
BIN := build/anvil

.PHONY: all clean install uninstall test smoke

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/anvil.h src/keywords_data.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c -o $@ $<

build/pty_smoke: scripts/pty_smoke.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< -lutil

clean:
	rm -rf build

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(BIN) "$(DESTDIR)$(BINDIR)/anvil"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/anvil"

test smoke: $(BIN) build/pty_smoke
	./build/pty_smoke
