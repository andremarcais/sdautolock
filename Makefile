CFLAGS=`pkg-config --cflags libsystemd`
LDFLAGS=`pkg-config --libs libsystemd`

myautolock: myautolock.o
	gcc $(LDFLAGS) myautolock.o -o $(@)

myautolock.o: myautolock.c
	gcc -c $(CFLAGS) myautolock.c -o $(@) -Wall

myautolock.c:

