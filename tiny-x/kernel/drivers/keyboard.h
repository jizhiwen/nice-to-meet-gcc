#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
int  keyboard_getchar(void);    /* blocking read of one char */
int  keyboard_poll(void);       /* non-blocking, returns -1 if empty */

/* Circular key buffer size */
#define KB_BUFSIZE 64

#endif /* KEYBOARD_H */
