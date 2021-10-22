CFLAGS=`pkg-config --cflags libsystemd xcb xcb-screensaver` -Wall -Wunused
LDFLAGS=`pkg-config --libs libsystemd xcb xcb-screensaver`

DATE=`git log -n 1 --format=%as HEAD`
SOURCE=myautolock `git log -n 1 --format=%h HEAD`

PREFIX="/usr/local"

all: myautolock myautolock.1

myautolock: myautolock.o
	gcc $(LDFLAGS) $? -o $@

myautolock.o: myautolock.c
	gcc -c $(CFLAGS) $? -o $@ -Wall

myautolock.c:

myautolock.1: myautolock.1.src
	sed "s/__DATE__/$(DATE)/g; s/__SOURCE__/$(SOURCE)/g" < $? > $@

myautolock.1.src:

install: myautolock myautolock.1
	install myautolock $(PREFIX)/bin/
	install myautolock.1 $(PREFIX)/share/man/man1/

uninstall:
	unlink $(PREFIX)/bin/myautolock

clean:
	rm myautolock.o myautolock myautolock.1
