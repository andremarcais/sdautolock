CFLAGS=`pkg-config --cflags libsystemd`
LDFLAGS=`pkg-config --libs libsystemd`
PREFIX="/usr/local"

myautolock: myautolock.o
	gcc $(LDFLAGS) myautolock.o -o $(@)

myautolock.o: myautolock.c
	gcc -c $(CFLAGS) myautolock.c -o $(@) -Wall

myautolock.c:

install: myautolock
	install myautolock $(PREFIX)/bin

uninstall:
	unlink $(PREFIX)/bin/myautolock

clean:
	rm myautolock.o myautolock
