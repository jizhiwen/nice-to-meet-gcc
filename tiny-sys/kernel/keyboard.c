#include "kernel.h"

static inline uint8_t inb(uint16_t port)
{
	uint8_t ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

#define PS2_DATA 0x60
#define PS2_STATUS 0x64

static const char scancode_ascii[128] = {
	0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
	'*', 0, ' '
};

char keyboard_getchar(void)
{
	for (;;) {
		if (serial_input_ready())
			return serial_readchar();

		if ((inb(PS2_STATUS) & 0x01) == 0)
			continue;

		uint8_t scancode = inb(PS2_DATA);
		if (scancode & 0x80)
			continue;
		if (scancode < 128 && scancode_ascii[scancode])
			return scancode_ascii[scancode];
	}
}
