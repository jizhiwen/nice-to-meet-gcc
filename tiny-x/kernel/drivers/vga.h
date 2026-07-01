#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

/* VGA text mode: 80x25 characters */
#define VGA_COLS    80
#define VGA_ROWS    25

/* VGA color codes */
typedef enum {
    VGA_BLACK   = 0, VGA_BLUE      = 1, VGA_GREEN   = 2, VGA_CYAN    = 3,
    VGA_RED     = 4, VGA_MAGENTA   = 5, VGA_BROWN   = 6, VGA_LGRAY   = 7,
    VGA_DGRAY   = 8, VGA_LBLUE     = 9, VGA_LGREEN  = 10, VGA_LCYAN  = 11,
    VGA_LRED    = 12, VGA_LMAGENTA = 13, VGA_YELLOW  = 14, VGA_WHITE  = 15,
} vga_color_t;

#define VGA_ATTR(fg, bg)    (((bg) << 4) | (fg))

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_puthex(uint64_t v);
void vga_putdec(uint64_t v);
void vga_set_color(uint8_t attr);
void vga_printf(const char *fmt, ...);

#endif /* VGA_H */
