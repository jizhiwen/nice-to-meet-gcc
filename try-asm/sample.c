
#include <stdio.h>

int main(void) {
    int a = 1;
    int b = 2;
    int c = 0;

    __asm__(
        "addl %2, %1\n\t"
        "movl %1, %0"
        : "=r"(c)
        : "r"(a), "r"(b)
    );

    printf("%d\n", c);
    return 0;
}
