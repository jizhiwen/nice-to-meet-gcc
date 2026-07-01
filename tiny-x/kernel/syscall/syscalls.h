#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>

/* Linux x86_64 syscall numbers (matching Linux ABI) */
#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_STAT       4
#define SYS_FSTAT      5
#define SYS_LSEEK      8
#define SYS_MMAP       9
#define SYS_MPROTECT   10
#define SYS_MUNMAP     11
#define SYS_BRK        12
#define SYS_IOCTL      16
#define SYS_ACCESS     21
#define SYS_EXIT       60
#define SYS_FORK       57
#define SYS_WAIT4      61
#define SYS_EXECVE     59
#define SYS_GETPID     39
#define SYS_GETPPID    110
#define SYS_GETCWD     79
#define SYS_CHDIR      80
#define SYS_ARCH_PRCTL 158
#define SYS_SET_ROBUST_LIST 273
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP 231
#define SYS_OPENAT     257
#define SYS_NEWFSTATAT 262
#define SYS_READLINK   89
#define SYS_WRITEV     20
#define SYS_CLOCK_GETTIME 228

/* Main dispatch function called by syscall.S */
int64_t syscall_dispatch(uint64_t nr,
                         uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6);

void syscalls_init(void);

#endif /* SYSCALLS_H */
