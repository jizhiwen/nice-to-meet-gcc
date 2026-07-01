#include "kernel.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static int serial_ready(void)
{
	return inb(COM1 + 5) & 0x20;
}

static void serial_putchar(char c)
{
	while (!serial_ready())
		;
	outb(COM1, (uint8_t)c);
}

static int serial_has_input(void)
{
	return inb(COM1 + 5) & 0x01;
}

static char serial_getchar(void)
{
	while (!serial_has_input())
		;
	return (char)inb(COM1);
}

void serial_init(void)
{
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x80);
	outb(COM1 + 0, 0x03);
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);
	outb(COM1 + 2, 0xC7);
	outb(COM1 + 4, 0x0B);
}

void serial_writechar(char c)
{
	serial_putchar(c);
}

char serial_readchar(void)
{
	return serial_getchar();
}

int serial_input_ready(void)
{
	return serial_has_input();
}
