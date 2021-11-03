override CFLAGS += `pkg-config --cflags libsystemd` -Wall -Wunused
override LDFLAGS += `pkg-config --libs libsystemd`

DATE = `git log -n 1 --format=%as HEAD`
VERSION = `git describe --tags --always`

PREFIX="/usr/local"

all: sdautolock sdautolock.1

sdautolock: sdautolock.o
	$(CC) $(LDFLAGS) $? -o $@

sdautolock.o: sdautolock.c
	$(CC) -c $(CFLAGS) $? -o $@ -Wall

sdautolock.c:

sdautolock.1: sdautolock.1.src
	sed "s/__DATE__/$(DATE)/g; s/__VERSION__/$(VERSION)/g" < $? > $@

sdautolock.1.src:

release: all
	git archive --format=tar --prefix=sdautolock/ HEAD > tmp.tar
	tar xf tmp.tar
	rm tmp.tar
	sed -i 's/^DATE =.*/DATE = '$(DATE)'/; s/^VERSION = .*/VERSION = '$(VERSION)'/' sdautolock/Makefile
	rm sdautolock/.gitignore
	tar c sdautolock | gzip > sdautolock-$(VERSION).tar.gz
	rm -r sdautolock

install: sdautolock sdautolock.1
	install sdautolock $(PREFIX)/bin/
	install sdautolock.1 $(PREFIX)/share/man/man1/

uninstall:
	unlink $(PREFIX)/bin/sdautolock

clean:
	rm -f sdautolock.o sdautolock sdautolock.1 sdautolock-*.tar.gz
