#include "fib.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc == 2) {
        init_fib();
        printf("Result: %li\n", js_fib(atoi(argv[1])));
        cleanup_fib();
    } else {
        fprintf(stderr, "invalid arguments\n");
    }
}
