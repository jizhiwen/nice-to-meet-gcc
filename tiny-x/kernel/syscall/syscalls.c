/*
 * syscalls.c - System call implementations
 *
 * Called from syscall_entry in syscall.S after register save.
 * Returns int64_t to RAX (negative = errno negated, per Linux ABI).
 */

#include "syscalls.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/paging.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include <string.h>

/* errno values */
#define ENOSYS   38
#define EBADF    9
#define EINVAL   22
#define ENOMEM   12
#define EFAULT   14
#define ENOENT   2
#define ECHILD   10
#define EAGAIN   11
#define EPERM    1

/* ── Console I/O ─────────────────────────────────────────────────────── */
static int64_t sys_write(uint64_t fd, const char *buf, size_t count)
{
    if (fd > 2 && fd >= MAX_FDS) return -EBADF;
    if (!buf) return -EFAULT;

    /* For stdout/stderr: write to VGA + serial */
    if (fd == 1 || fd == 2) {
        /* Copy from user address space */
        extern process_t *current;
        vmm_aspace_t *as = current ? current->aspace : 0;
        for (size_t i = 0; i < count; i++) {
            char c;
            if (as) {
                uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)(buf + i) & PAGE_MASK);
                if (!pa) return -EFAULT;
                c = *(char *)(pa + ((uint64_t)(buf + i) & (PAGE_SIZE - 1)));
            } else {
                c = buf[i];
            }
            vga_putchar(c);
            extern void serial_putchar(char);
            serial_putchar(c);
        }
        return (int64_t)count;
    }

    /* File descriptor write */
    if (current && fd < MAX_FDS && current->fds[fd].type == 4) {
        vfs_node_t *node = (vfs_node_t *)current->fds[fd].data;
        if (node) {
            ssize_t r = vfs_write(node, buf, count, (size_t)current->fds[fd].pos);
            if (r > 0) current->fds[fd].pos += (int)r;
            return r;
        }
    }
    return -EBADF;
}

static int64_t sys_read(uint64_t fd, char *buf, size_t count)
{
    if (!buf || count == 0) return 0;

    if (fd == 0) {
        /* stdin: read from keyboard */
        extern process_t *current;
        vmm_aspace_t *as = current ? current->aspace : 0;
        size_t i = 0;
        while (i < count) {
            int c = keyboard_getchar();
            if (c < 0) break;
            char ch = (char)c;
            /* Echo */
            vga_putchar(ch);
            extern void serial_putchar(char);
            serial_putchar(ch);

            /* Write to user buffer */
            if (as) {
                uint64_t pa = paging_virt_to_phys(as->pml4,
                                (uint64_t)(buf + i) & PAGE_MASK);
                if (!pa) return -EFAULT;
                *(char *)(pa + ((uint64_t)(buf + i) & (PAGE_SIZE - 1))) = ch;
            } else {
                buf[i] = ch;
            }
            i++;
            if (ch == '\n') break;  /* return on newline */
        }
        return (int64_t)i;
    }

    if (current && fd < MAX_FDS && current->fds[fd].type == 4) {
        vfs_node_t *node = (vfs_node_t *)current->fds[fd].data;
        if (node) {
            ssize_t r = vfs_read(node, buf, count, (size_t)current->fds[fd].pos);
            if (r > 0) current->fds[fd].pos += (int)r;
            return r;
        }
    }
    return -EBADF;
}

static int64_t sys_open(const char *path, int flags, int mode)
{
    (void)flags; (void)mode;
    if (!path) return -EFAULT;

    /* Copy path from user space */
    char kpath[VFS_PATH_MAX];
    extern process_t *current;
    vmm_aspace_t *as = current ? current->aspace : 0;
    for (int i = 0; i < VFS_PATH_MAX; i++) {
        char c;
        if (as) {
            uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)(path + i) & PAGE_MASK);
            if (!pa) return -EFAULT;
            c = *(char *)(pa + ((uint64_t)(path + i) & (PAGE_SIZE - 1)));
        } else {
            c = path[i];
        }
        kpath[i] = c;
        if (!c) break;
    }
    kpath[VFS_PATH_MAX - 1] = '\0';

    vfs_node_t *node = vfs_lookup(kpath);
    if (!node) return -ENOENT;

    /* Find free fd */
    for (int fd = 3; fd < MAX_FDS; fd++) {
        if (current->fds[fd].type == 0) {
            current->fds[fd].type = 4;
            current->fds[fd].data = node;
            current->fds[fd].pos  = 0;
            return fd;
        }
    }
    return -ENOMEM;
}

static int64_t sys_close(uint64_t fd)
{
    if (fd < 3 || fd >= MAX_FDS) return -EBADF;
    extern process_t *current;
    current->fds[fd].type = 0;
    current->fds[fd].data = 0;
    return 0;
}

/* ── Memory management ───────────────────────────────────────────────── */
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define MAP_ANON    0x20
#define MAP_PRIVATE 0x02

static int64_t sys_mmap(uint64_t addr, size_t length, int prot, int flags,
                         int fd, uint64_t offset)
{
    (void)fd; (void)offset;
    extern process_t *current;
    if (!current) return -EFAULT;

    uint32_t vma_flags = 0;
    if (prot & PROT_READ)  vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;
    vma_flags |= VMA_ANON;

    /* Find a free virtual address if addr == 0 */
    if (!addr) {
        addr = 0x7F0000000000UL - length;
        /* Simple: use the brk area or a fixed mmap area */
        /* For now use a static bump allocator starting at 0x7000000 */
        static uint64_t mmap_bump = 0x7000000UL;
        addr = mmap_bump;
        mmap_bump += PAGE_ALIGN(length);
    }

    if (vmm_map(current->aspace, addr, length, vma_flags) < 0) return -ENOMEM;
    return (int64_t)addr;
}

static int64_t sys_munmap(uint64_t addr, size_t length)
{
    extern process_t *current;
    if (current) vmm_unmap(current->aspace, addr, length);
    return 0;
}

static int64_t sys_brk(uint64_t new_brk)
{
    extern process_t *current;
    if (!current || !current->aspace) return -EFAULT;
    uint64_t ret = vmm_brk(current->aspace, new_brk);
    return (int64_t)ret;
}

/* ── Process management ──────────────────────────────────────────────── */
static int64_t sys_fork(void)
{
    return proc_fork();
}

static int64_t sys_execve(const char *path, const char **argv, const char **envp)
{
    /* Copy path */
    char kpath[VFS_PATH_MAX];
    extern process_t *current;
    vmm_aspace_t *as = current ? current->aspace : 0;
    for (int i = 0; i < VFS_PATH_MAX; i++) {
        char c;
        if (as) {
            uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)(path + i) & PAGE_MASK);
            if (!pa) return -EFAULT;
            c = *(char *)(pa + ((uint64_t)(path + i) & (PAGE_SIZE - 1)));
        } else {
            c = path[i];
        }
        kpath[i] = c;
        if (!c) break;
    }

    /* Copy argv (limited to 64 entries) */
    const char *kargv[64];
    memset(kargv, 0, sizeof(kargv));
    int argc = 0;
    if (argv && as) {
        for (int i = 0; i < 63; i++) {
            uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)(argv + i) & PAGE_MASK);
            if (!pa) break;
            uint64_t ptr = *(uint64_t *)(pa + ((uint64_t)(argv + i) & (PAGE_SIZE - 1)));
            if (!ptr) break;
            kargv[argc++] = (const char *)ptr;
        }
    }
    /* For envp, pass NULL for simplicity (can be extended) */
    const char *kenvp[] = { "PATH=/bin", "HOME=/", 0 };

    return proc_exec(kpath, (const char **)kargv, kenvp);
}

static int64_t sys_exit(int code)
{
    proc_exit(code);
    return 0;  /* never reached */
}

static int64_t sys_wait4(int pid, int *wstatus, int options, void *rusage)
{
    (void)options; (void)rusage;
    return proc_wait(pid, wstatus);
}

static int64_t sys_getpid(void)
{
    extern process_t *current;
    return current ? current->pid : 1;
}

/* ── arch_prctl (used by glibc/musl for TLS) ─────────────────────────── */
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_SET_GS 0x1001

static int64_t sys_arch_prctl(int code, uint64_t addr)
{
    switch (code) {
    case ARCH_SET_FS:
        /* Set FS base (for TLS) */
        __asm__ volatile("wrfsbase %0" : : "r"(addr));
        return 0;
    case ARCH_GET_FS: {
        uint64_t fs;
        __asm__ volatile("rdfsbase %0" : "=r"(fs));
        return (int64_t)fs;
    }
    case ARCH_SET_GS:
        return 0;
    }
    return -EINVAL;
}

static int64_t sys_getcwd(char *buf, size_t size)
{
    if (!buf || size < 2) return -EINVAL;
    /* Always return "/" for now */
    extern process_t *current;
    vmm_aspace_t *as = current ? current->aspace : 0;
    if (as) {
        uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)buf & PAGE_MASK);
        if (pa) {
            char *dst = (char *)(pa + ((uint64_t)buf & (PAGE_SIZE - 1)));
            dst[0] = '/'; dst[1] = '\0';
        }
    }
    return (int64_t)buf;
}

/* ── writev ──────────────────────────────────────────────────────────── */
typedef struct { const void *iov_base; size_t iov_len; } iovec_t;

static int64_t sys_writev(uint64_t fd, const iovec_t *iov, int iovcnt)
{
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        int64_t r = sys_write(fd, (const char *)iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return r;
        total += r;
    }
    return total;
}

/* ── stat structures ─────────────────────────────────────────────────── */
typedef struct {
    uint64_t st_dev, st_ino;
    uint64_t st_nlink;
    uint32_t st_mode, st_uid, st_gid;
    uint32_t pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize, st_blocks;
    uint64_t st_atim[2], st_mtim[2], st_ctim[2];
    uint64_t pad[3];
} stat_t;

static int64_t sys_fstat(int fd, stat_t *st)
{
    extern process_t *current;
    vmm_aspace_t *as = current ? current->aspace : 0;
    if (!as) return -EFAULT;

    uint64_t pa = paging_virt_to_phys(as->pml4, (uint64_t)st & PAGE_MASK);
    if (!pa) return -EFAULT;
    stat_t *kst = (stat_t *)(pa + ((uint64_t)st & (PAGE_SIZE - 1)));
    memset(kst, 0, sizeof(*kst));

    if ((uint64_t)fd <= 2) {
        kst->st_mode = 0x2180;  /* S_IFCHR | 0600 */
        return 0;
    }
    if (current && (uint64_t)fd < MAX_FDS && current->fds[fd].type == 4) {
        vfs_node_t *node = (vfs_node_t *)current->fds[fd].data;
        kst->st_size  = (int64_t)node->size;
        kst->st_mode  = 0x8180;  /* S_IFREG | 0600 */
        return 0;
    }
    return -EBADF;
}

/* ── Main dispatch ───────────────────────────────────────────────────── */
int64_t syscall_dispatch(uint64_t nr,
                          uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a5; (void)a6;

    switch (nr) {
    case SYS_READ:       return sys_read(a1, (char *)a2, (size_t)a3);
    case SYS_WRITE:      return sys_write(a1, (const char *)a2, (size_t)a3);
    case SYS_OPEN:       return sys_open((const char *)a1, (int)a2, (int)a3);
    case SYS_CLOSE:      return sys_close(a1);
    case SYS_FSTAT:      return sys_fstat((int)a1, (stat_t *)a2);
    case SYS_MMAP:       return sys_mmap(a1, (size_t)a2, (int)a3,
                                          (int)a4, (int)a5, a6);
    case SYS_MPROTECT:   return 0;  /* stub: always succeed */
    case SYS_MUNMAP:     return sys_munmap(a1, (size_t)a2);
    case SYS_BRK:        return sys_brk(a1);
    case SYS_IOCTL:      return 0;
    case SYS_ACCESS:     return 0;
    case SYS_FORK:       return sys_fork();
    case SYS_EXECVE:     return sys_execve((const char *)a1,
                                            (const char **)a2,
                                            (const char **)a3);
    case SYS_EXIT:
    case SYS_EXIT_GROUP: return sys_exit((int)a1);
    case SYS_WAIT4:      return sys_wait4((int)a1, (int *)a2, (int)a3, (void *)a4);
    case SYS_GETPID:     return sys_getpid();
    case SYS_GETPPID:    return current ? current->ppid : 0;
    case SYS_GETCWD:     return sys_getcwd((char *)a1, (size_t)a2);
    case SYS_CHDIR:      return 0;  /* stub */
    case SYS_ARCH_PRCTL: return sys_arch_prctl((int)a1, a2);
    case SYS_SET_TID_ADDRESS: return sys_getpid();  /* stub */
    case SYS_SET_ROBUST_LIST: return 0;
    case SYS_WRITEV:     return sys_writev(a1, (const iovec_t *)a2, (int)a3);
    case SYS_CLOCK_GETTIME: return 0;  /* stub */
    case SYS_OPENAT:     return sys_open((const char *)a2, (int)a3, 0);
    case SYS_READLINK:   return -ENOENT;
    case SYS_NEWFSTATAT: {
        if (a3 == 0) return -ENOENT;
        vfs_node_t *node = vfs_lookup((const char *)a2);
        if (!node) return -ENOENT;
        return 0;
    }
    default:
        vga_printf("[syscall] unknown nr=%llu\n", (unsigned long long)nr);
        return -ENOSYS;
    }
}

void syscalls_init(void) { /* nothing to do */ }
