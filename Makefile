PREFIX = /usr/local

CC = gcc
CFLAGS = -O -Wall
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man

.PHONY: all
all: sterm

.PHONY: clean
clean:
	rm -f sterm sterm.o

.PHONY: install
install: all
	install -s sterm $(BINDIR)
	install sterm.1 $(MANDIR)/man1

sterm: sterm.o
