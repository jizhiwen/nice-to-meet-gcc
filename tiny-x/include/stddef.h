#ifndef _STDDEF_H
#define _STDDEF_H

/* size_t and ptrdiff_t for x86_64 (LP64 model) */
typedef unsigned long  size_t;
typedef signed long    ptrdiff_t;

/* wchar_t */
typedef int            wchar_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* offsetof: use GCC's built-in (pure compiler intrinsic, no library) */
#define offsetof(type, member)  __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
