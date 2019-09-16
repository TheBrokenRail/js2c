# JS2C

This program allows easily making binding to use JS librries in C projects.

## Building

```bash
./deps.sh
mkdir build && cd build
cmake ..
make
```

## Usage

### Shared Library

```bash
$ js2c -N fib -o example/libfib.so example/fib.js example/fib.c
$ clang -Lexample -o example/fib example/test.c -lfib -Wl,-rpath,example
$ example/fib 10
Result: 55
```

### C File

```bash
$ js2c -e -N fib -o example/libfib.c example/fib.js example/fib.c
$ clang -I<prefix>/include/js2c -o example/fib example/libfib.c example/test.c -ljs2c
$ example/fib 10
Result: 55
```

### Object File

```bash
$ js2c -c -N fib -o example/libfib.o example/fib.js example/fib.c
$ clang -o example/fib example/libfib.o example/test.c -ljs2c
$ example/fib 10
Result: 55
```

## Header Files

Header files are not automatically generated. You must make them yourself. An example of a header file is in the ```example``` directory. The program name to be used in your init_<>(), and cleanup_<>() methods is by default ```js_library``` but can be overriden by the -N argument.

## Multi-threading

One a library is initialized it can only be used on the thread it has been initialized on however, once a library has been cleaned up it can be reinitiazlied on another thread.
