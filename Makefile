override CFLAGS += `pkg-config --cflags libsystemd xcb xcb-screensaver` -Wall -Wunused
override LDFLAGS += `pkg-config --libs libsystemd xcb xcb-screensaver`

DATE = `git log -n 1 --format=%as HEAD`
SOURCE = sdautolock `git log -n 1 --format=%h HEAD`

PREFIX="/usr/local"

all: sdautolock sdautolock.1

sdautolock: sdautolock.o
	$(CC) $(LDFLAGS) $? -o $@

sdautolock.o: sdautolock.c
	$(CC) -c $(CFLAGS) $? -o $@ -Wall

sdautolock.c:

sdautolock.1: sdautolock.1.src
	sed "s/__DATE__/$(DATE)/g; s/__SOURCE__/$(SOURCE)/g" < $? > $@

sdautolock.1.src:

install: sdautolock sdautolock.1
	install sdautolock $(PREFIX)/bin/
	install sdautolock.1 $(PREFIX)/share/man/man1/

uninstall:
	unlink $(PREFIX)/bin/sdautolock

clean:
	rm sdautolock.o sdautolock sdautolock.1
