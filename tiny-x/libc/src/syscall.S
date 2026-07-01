/*
 * syscall.S - Userspace system call wrappers
 *
 * Linux x86_64 syscall ABI:
 *   RAX = syscall number
 *   RDI, RSI, RDX = args 1-3
 *   R10 = arg 4  (note: C passes in RCX, we move to R10)
 *   R8, R9 = args 5-6
 *   SYSCALL instruction
 *   Return: RAX (negative = -errno)
 *
 * All wrappers follow C calling convention (rdi,rsi,rdx,rcx,r8,r9 for args)
 * We just fix up RCX→R10 for arg4 and set RAX=syscall number.
 */

.intel_syntax noprefix

/* ── Generic syscall macro ─────────────────────────────────────────── */
/* syscallN(name, number, nargs) */

.macro SYSCALL1 name, nr
.global \name
\name:
    mov rax, \nr
    syscall
    ret
.endm

.macro SYSCALL2 name, nr
.global \name
\name:
    mov rax, \nr
    syscall
    ret
.endm

.macro SYSCALL3 name, nr
.global \name
\name:
    mov rax, \nr
    syscall
    ret
.endm

/* For 4+ args: RCX (arg4 in C ABI) must be moved to R10 */
.macro SYSCALL4 name, nr
.global \name
\name:
    mov r10, rcx
    mov rax, \nr
    syscall
    ret
.endm

.macro SYSCALL6 name, nr
.global \name
\name:
    mov r10, rcx
    mov rax, \nr
    syscall
    ret
.endm

/* ── Syscall wrappers ──────────────────────────────────────────────── */
SYSCALL3  read,      0
SYSCALL3  write,     1
SYSCALL3  _sys_open, 2      /* called open() from open.c */
SYSCALL1  close,     3
SYSCALL2  _fstat,    5
SYSCALL3  lseek,     8
SYSCALL6  mmap,      9
SYSCALL3  mprotect,  10
SYSCALL2  munmap,    11
SYSCALL1  brk,       12
SYSCALL3  ioctl,     16
SYSCALL2  access,    21
SYSCALL1  pipe,      22     /* pipe(pipefd[2]) */
SYSCALL3  dup2,      33     /* dup2(oldfd, newfd) */
SYSCALL1  getpid,    39
SYSCALL1  fork,      57
SYSCALL1  _exit,     60
SYSCALL3  execve,    59
SYSCALL4  wait4,     61
SYSCALL2  getcwd,    79
SYSCALL1  chdir,     80
SYSCALL3  readlink,  89
SYSCALL3  writev,    20
SYSCALL2  arch_prctl,158
SYSCALL2  set_tid_address, 218
SYSCALL2  set_robust_list, 273
SYSCALL1  exit_group, 231
SYSCALL2  clock_gettime, 228
SYSCALL4  openat,    257

/* ── open() wrapper: handles variadic mode arg ─────────────────────── */
.global open
open:
    /* open(path, flags, mode) - mode is in rdx if present */
    mov rax, 2      /* SYS_OPEN */
    syscall
    ret

/* ── errno handling ────────────────────────────────────────────────── */
/*
 * Most syscall wrappers just return raw rax.
 * The C wrappers in syscall.c check for negative and set errno.
 */
