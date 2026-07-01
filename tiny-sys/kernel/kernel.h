#pragma once

#include <stddef.h>
#include <stdint.h>

void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info);

void terminal_init(void);
void terminal_clear(void);
void terminal_putchar(char c);
void terminal_write(const char *data, size_t length);
void terminal_writestring(const char *data);

void serial_init(void);
void serial_writechar(char c);
char serial_readchar(void);
int serial_input_ready(void);
void terminal_setcolor(uint8_t color);

char keyboard_getchar(void);

void shell_run(void);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
void *memset(void *s, int c, size_t n);
