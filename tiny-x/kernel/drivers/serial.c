/* serial.c - COM1 serial port for kernel debug output */
#include "serial.h"
#include <stdint.h>

#define COM1    0x3F8

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));
    return v;
}

void serial_init(void) {
    outb(COM1+1, 0x00);   /* disable interrupts */
    outb(COM1+3, 0x80);   /* DLAB on */
    outb(COM1+0, 0x03);   /* 38400 baud divisor lo */
    outb(COM1+1, 0x00);   /* divisor hi */
    outb(COM1+3, 0x03);   /* 8N1 */
    outb(COM1+2, 0xC7);   /* FIFO enable, clear, 14-byte threshold */
    outb(COM1+4, 0x0B);   /* IRQs enabled, RTS/DSR set */
}

void serial_putchar(char c) {
    while (!(inb(COM1+5) & 0x20));  /* wait for TX empty */
    if (c == '\n') { outb(COM1, '\r'); while (!(inb(COM1+5) & 0x20)); }
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putchar(*s++);
}
