#ifndef LIBC_TINY_H
#define LIBC_TINY_H

#include <stddef.h>
#include <stdint.h>

long tiny_syscall0(long n);
long tiny_syscall1(long n, long a1);
long tiny_syscall2(long n, long a1, long a2);
long tiny_syscall3(long n, long a1, long a2, long a3);
long tiny_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6);

void tiny_exit(int code);
long tiny_write(int fd, const void* buf, unsigned long count);
unsigned long tiny_strlen(const char* s);
long tiny_puts(const char* s);
void tiny_print_int(long value);
void* tiny_memcpy(void* dst, const void* src, unsigned long n);
void* tiny_memset(void* dst, int c, unsigned long n);
void* tiny_malloc(unsigned long n);
void tiny_free(void* p);
void tiny_abort(void);

#endif
