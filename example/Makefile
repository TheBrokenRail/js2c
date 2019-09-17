CC := clang

all: fib

libfib.so:
	js2c -N fib -o $@ fib.c fib.js

fib: libfib.so
	$(CC) -o $@ -L. test.c -lfib -Wl,-rpath,.

clean:
	rm -rf libfib.so
	rm -rf fib

.PHONY: all clean
