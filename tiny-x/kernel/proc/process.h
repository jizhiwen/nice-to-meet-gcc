#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "../mm/vmm.h"

#define MAX_PROCESSES   64
#define MAX_FDS         32
#define PROC_NAME_LEN   64
#define KSTACK_SIZE     (16 * 1024)  /* 16KB kernel stack per process */

/* Process states */
typedef enum {
    PROC_UNUSED  = 0,
    PROC_READY   = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,   /* waiting for I/O */
    PROC_ZOMBIE  = 4,   /* exited, waiting for parent */
    PROC_WAITING = 5,   /* waitpid */
} proc_state_t;

/* File descriptor entry */
typedef struct {
    int      type;      /* 0=free, 1=stdin, 2=stdout, 3=stderr, 4=file */
    int      flags;
    int      pos;       /* for file: current offset */
    void    *data;      /* for file: pointer to vfs_node */
} fd_entry_t;

/* Saved register state (for context switch) */
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t rbp, rbx;
    uint64_t rsp;       /* kernel stack pointer */
    uint64_t rip;       /* kernel instruction pointer (return address) */
} cpu_ctx_t;

/* Process control block */
typedef struct process {
    int             pid;
    int             ppid;
    proc_state_t    state;
    char            name[PROC_NAME_LEN];

    vmm_aspace_t   *aspace;         /* virtual address space */

    cpu_ctx_t       ctx;            /* saved kernel context (for context switch) */
    uint64_t        user_rsp;       /* user-space stack pointer */
    uint64_t        user_rip;       /* user-space entry point */
    uint64_t        kstack_top;     /* top of kernel stack */
    uint8_t        *kstack;         /* kernel stack base */

    fd_entry_t      fds[MAX_FDS];   /* file descriptors */
    int             exit_code;
    int             wait_pid;       /* pid we're waiting for (-1 = any) */

    struct process *next;
} process_t;

/* CPU-local data (GS-based, used by syscall.S) */
typedef struct {
    uint64_t    self;           /* gs:0x00 – pointer to this struct */
    uint64_t    kernel_rsp;     /* gs:0x08 – kernel stack for syscall entry */
    uint64_t    user_rsp;       /* gs:0x10 – scratch for saving user RSP */
    process_t  *current;        /* gs:0x18 – current process */
} cpu_local_t;

extern cpu_local_t cpu_local;
extern process_t  *current;

/* Process operations */
void  proc_init(void);
process_t *proc_new(const char *name);
void  proc_free(process_t *p);

int   proc_fork(void);              /* returns child pid to parent, 0 to child */
int   proc_exec(const char *path, const char **argv, const char **envp);
void  proc_exit(int code);
int   proc_wait(int pid, int *status);

/* Scheduler */
void  sched_init(void);
void  sched_add(process_t *p);
void  sched_yield(void);            /* voluntary switch */
void  schedule(void);               /* pick next process */

/* Context switch (in process.S or inline) */
void  ctx_switch(cpu_ctx_t *old_ctx, cpu_ctx_t *new_ctx);

/* Jump to user space */
void  jump_to_userspace(uint64_t rip, uint64_t rsp, uint64_t cr3);

#endif /* PROCESS_H */
