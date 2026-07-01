#ifndef _STDARG_H
#define _STDARG_H

/*
 * Variadic argument support for x86_64.
 *
 * We use GCC's __builtin_va_* intrinsics which are part of the COMPILER
 * itself (code generation), not any library. They emit the correct
 * System V AMD64 ABI register-save-area / overflow-area accesses.
 *
 * This is the standard approach for all bare-metal OS and freestanding
 * code - even Linux kernel uses __builtin_va_*.
 */

typedef __builtin_va_list va_list;

#define va_start(ap, last)  __builtin_va_start(ap, last)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)
#define va_end(ap)          __builtin_va_end(ap)
#define va_copy(dst, src)   __builtin_va_copy(dst, src)

#endif /* _STDARG_H */
