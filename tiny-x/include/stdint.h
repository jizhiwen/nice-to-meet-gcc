#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width integer types for x86_64 */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* Pointer-sized integer types (x86_64: 64-bit) */
typedef unsigned long       uintptr_t;
typedef signed long         intptr_t;

/* Widest integer types */
typedef unsigned long long  uintmax_t;
typedef signed long long    intmax_t;

/* Least/fast types */
typedef uint8_t             uint_least8_t;
typedef uint16_t            uint_least16_t;
typedef uint32_t            uint_least32_t;
typedef uint64_t            uint_least64_t;
typedef int8_t              int_least8_t;
typedef int16_t             int_least16_t;
typedef int32_t             int_least32_t;
typedef int64_t             int_least64_t;

typedef uint8_t             uint_fast8_t;
typedef uint64_t            uint_fast16_t;
typedef uint64_t            uint_fast32_t;
typedef uint64_t            uint_fast64_t;
typedef int8_t              int_fast8_t;
typedef int64_t             int_fast16_t;
typedef int64_t             int_fast32_t;
typedef int64_t             int_fast64_t;

/* Limits */
#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define UINT8_MAX  (0xFFU)

#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define UINT16_MAX (0xFFFFU)

#define INT32_MIN  (-2147483648)
#define INT32_MAX  (2147483647)
#define UINT32_MAX (0xFFFFFFFFU)

#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  (9223372036854775807LL)
#define UINT64_MAX (0xFFFFFFFFFFFFFFFFULL)

#define INTPTR_MIN   INT64_MIN
#define INTPTR_MAX   INT64_MAX
#define UINTPTR_MAX  UINT64_MAX
#define SIZE_MAX     UINT64_MAX

/* Format macros */
#define PRIu64 "llu"
#define PRId64 "lld"
#define PRIx64 "llx"

#endif /* _STDINT_H */
