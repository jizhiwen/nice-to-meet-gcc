/*
 * vga.c - VGA text mode driver
 *
 * Physical base address: 0xB8000
 * Each cell = 2 bytes: [char][attr]
 * attr = (bg<<4)|fg  (each nibble is a 4-bit color)
 */

#include "vga.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define VGA_BASE    ((volatile uint16_t *)0xB8000)
#define VGA_CMD     0x3D4
#define VGA_DATA    0x3D5

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = VGA_ATTR(VGA_LGRAY, VGA_BLACK);

static inline uint16_t make_cell(char c, uint8_t attr)
{
    return (uint16_t)((uint16_t)attr << 8) | (uint8_t)c;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_COLS + vga_col);
    outb(VGA_CMD, 0x0F);
    outb(VGA_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CMD, 0x0E);
    outb(VGA_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_clear(void)
{
    uint16_t blank = make_cell(' ', vga_attr);
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BASE[i] = blank;
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_init(void)
{
    vga_attr = VGA_ATTR(VGA_LGRAY, VGA_BLACK);
    vga_clear();
}

static void vga_scroll(void)
{
    /* Move all rows up by one */
    for (int r = 0; r < VGA_ROWS - 1; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            VGA_BASE[r * VGA_COLS + c] = VGA_BASE[(r+1) * VGA_COLS + c];
        }
    }
    /* Clear last row */
    uint16_t blank = make_cell(' ', vga_attr);
    for (int c = 0; c < VGA_COLS; c++)
        VGA_BASE[(VGA_ROWS-1) * VGA_COLS + c] = blank;
    vga_row = VGA_ROWS - 1;
}

void vga_putchar(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_BASE[vga_row * VGA_COLS + vga_col] = make_cell(' ', vga_attr);
        }
    } else {
        VGA_BASE[vga_row * VGA_COLS + vga_col] = make_cell(c, vga_attr);
        vga_col++;
    }

    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_ROWS) {
        vga_scroll();
    }
    vga_update_cursor();
}

void vga_puts(const char *s)
{
    while (*s) vga_putchar(*s++);
}

void vga_set_color(uint8_t attr)
{
    vga_attr = attr;
}

void vga_puthex(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    vga_puts("0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int nibble = (v >> i) & 0xF;
        if (nibble || started || i == 0) {
            vga_putchar(hex[nibble]);
            started = 1;
        }
    }
}

void vga_putdec(uint64_t v)
{
    if (v == 0) { vga_putchar('0'); return; }
    char buf[20];
    int i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) vga_putchar(buf[--i]);
}

/* Minimal printf for kernel debugging */
void vga_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putchar(*fmt++);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 'd': {
            long long v = va_arg(ap, long long);
            if (v < 0) { vga_putchar('-'); v = -v; }
            vga_putdec((uint64_t)v);
            break;
        }
        case 'u':
            vga_putdec(va_arg(ap, unsigned long long));
            break;
        case 'x': case 'X':
            vga_puthex(va_arg(ap, unsigned long long));
            break;
        case 'p':
            vga_puthex((uint64_t)va_arg(ap, void *));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            vga_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            vga_putchar((char)va_arg(ap, int));
            break;
        case '%':
            vga_putchar('%');
            break;
        default:
            vga_putchar('%');
            vga_putchar(*fmt);
        }
        fmt++;
    }
    va_end(ap);
}
