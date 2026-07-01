#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef long     ssize_t;
typedef long     off_t;
typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef uint64_t ino_t;
typedef uint64_t dev_t;
typedef uint64_t nlink_t;
typedef int64_t  time_t;

#endif /* KERNEL_TYPES_H */
