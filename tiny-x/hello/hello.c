/*
 * hello.c - Hello World (dynamically linked)
 *
 * This program demonstrates dynamic linking:
 *   - Uses printf from libc.so (runtime-resolved via PLT/GOT)
 *   - The dynamic linker (ld-tiny.so) loads and links it
 *   - On first printf call: PLT stub → _dl_runtime_resolve → patch GOT
 *   - On subsequent calls: PLT stub → GOT → printf directly
 *
 * Compile with: gcc -fPIE -pie -nostdlib -nostdinc
 *               -I../libc/include hello.c -L../libc -lc -o hello
 *   or our tiny build system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    printf("Hello, World!\n");
    printf("Running on tiny-x OS\n");
    printf("argc = %d\n", argc);
    if (argc > 0) printf("argv[0] = %s\n", argv[0]);

    /* Demonstrate that libc functions work */
    char *s = strdup("tiny-x dynamic linking works!");
    if (s) {
        printf("strdup: %s\n", s);
        free(s);
    }

    /* Show process info */
    printf("PID: %d\n", (int)getpid());

    return 0;
}
