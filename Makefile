PREFIX = /usr/local

CC = gcc
CFLAGS = -O -Wall
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man

.PHONY: all
all: microcom

.PHONY: clean
clean:
	rm -f microcom microcom.o

.PHONY: install
install: all
	install -s microcom $(BINDIR)
	install microcom.1 $(MANDIR)/man1

microcom: microcom.o
