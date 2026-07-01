/*
 * process.c - Process management
 *
 * Implements fork/exec/exit/wait and the round-robin scheduler.
 *
 * Context switching:
 *   Kernel context is saved/restored on the kernel stack.
 *   User context is saved/restored via the syscall entry frame.
 */

#include "process.h"
#include "elf.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/gdt.h"
#include "../fs/vfs.h"
#include "../drivers/vga.h"
#include <string.h>

/* ── Global process table ────────────────────────────────────────────── */
static process_t proctab[MAX_PROCESSES];
static int       proctab_used[MAX_PROCESSES];
process_t       *current = 0;

/* Run queue: circular linked list */
static process_t *run_queue_head = 0;

/* CPU-local data (accessed via GS in syscall.S) */
cpu_local_t cpu_local __attribute__((aligned(16)));

/* ── Allocate a new PID ───────────────────────────────────────────────── */
static int next_pid = 1;
static int alloc_pid(void) { return next_pid++; }

/* ── Find a free process slot ────────────────────────────────────────── */
process_t *proc_new(const char *name)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!proctab_used[i]) {
            proctab_used[i] = 1;
            process_t *p = &proctab[i];
            memset(p, 0, sizeof(*p));
            p->pid   = alloc_pid();
            p->state = PROC_READY;
            /* Allocate kernel stack */
            p->kstack     = (uint8_t *)pmm_alloc_n(KSTACK_SIZE / 4096);
            p->kstack_top = (uint64_t)p->kstack + KSTACK_SIZE;
            if (name) {
                size_t len = __builtin_strlen(name);
                if (len >= PROC_NAME_LEN) len = PROC_NAME_LEN - 1;
                memcpy(p->name, name, len);
                p->name[len] = '\0';
            }
            /* stdin/stdout/stderr = console (type 1=tty) */
            p->fds[0].type = 1;
            p->fds[1].type = 1;
            p->fds[2].type = 1;
            return p;
        }
    }
    return 0;
}

void proc_free(process_t *p)
{
    if (p->aspace) vmm_free_aspace(p->aspace);
    if (p->kstack) pmm_free((uint64_t)p->kstack);
    int idx = (int)(p - proctab);
    if (idx >= 0 && idx < MAX_PROCESSES)
        proctab_used[idx] = 0;
}

void proc_init(void)
{
    memset(proctab, 0, sizeof(proctab));
    memset(proctab_used, 0, sizeof(proctab_used));
    current = 0;
}

/* ── Scheduler ───────────────────────────────────────────────────────── */
void sched_init(void) { run_queue_head = 0; }

void sched_add(process_t *p)
{
    if (!run_queue_head) {
        run_queue_head = p;
        p->next = p;
    } else {
        /* Insert after head */
        p->next = run_queue_head->next;
        run_queue_head->next = p;
    }
}

static void sched_remove(process_t *p)
{
    if (!run_queue_head) return;
    if (run_queue_head == p && p->next == p) {
        run_queue_head = 0;
        p->next = 0;
        return;
    }
    process_t *prev = run_queue_head;
    while (prev->next != p && prev->next != run_queue_head) prev = prev->next;
    if (prev->next == p) {
        prev->next = p->next;
        if (run_queue_head == p) run_queue_head = p->next;
        p->next = 0;
    }
}

/*
 * jump_to_userspace - switch from kernel to user mode
 *
 * Uses SYSRET to jump to user RIP with user RSP.
 * We set up a fake "syscall return" frame.
 */
void __attribute__((noreturn))
jump_to_userspace(uint64_t rip, uint64_t rsp, uint64_t cr3)
{
    /* Load the process's page table */
    paging_load_cr3((pte_t *)cr3);

    /* Update TSS.RSP0 to point to our kernel stack top */
    tss_set_rsp0(current->kstack_top);

    /* Update CPU local kernel RSP */
    cpu_local.kernel_rsp = current->kstack_top;

    /*
     * SYSRET expects:
     *   RCX = user RIP
     *   R11 = user RFLAGS (0x202 = IF set)
     *   RSP = user stack
     * We also need to set data segment selectors for user space.
     *
     * Our GDT layout for SYSRET:
     *   SEG_UDATA|RPL3 = 0x23 (udata, STAR[63:48]=0x10 → SS=(0x10+8)|3=0x1B)
     *   Wait, our GDT is: 0=null,8=kcode,16=kdata,24=ucode,32=udata
     *   and in boot.S: 0x18=ucode, 0x20=udata
     *   STAR was set with [63:48]=0x10:
     *     SYSRET64: CS=(0x10+16)|3=0x23, SS=(0x10+8)|3=0x1B
     *   0x23>>3=4=udata descriptor, 0x1B>>3=3=ucode descriptor
     *   That's wrong! We need SS→udata and CS→ucode.
     *
     * Fix: swap ucode/udata in GDT, or adjust STAR.
     *   With GDT: 0=null,8=kcode,16=kdata,24=udata,32=ucode  (udata before ucode)
     *   STAR[63:48]=0x10: CS=(0x10+16)|3=0x23=32|3=ucode ✓
     *                     SS=(0x10+8)|3=0x1B=24|3=udata ✓
     * But we defined ucode=0x18, udata=0x20 in gdt.h/boot.S...
     * For now use iretq instead which is more flexible:
     */
    __asm__ volatile(
        /* Push iretq frame: SS, RSP, RFLAGS, CS, RIP */
        "push %[ss]         \n"   /* SS: user data */
        "push %[rsp]        \n"   /* user RSP */
        "pushf              \n"   /* RFLAGS */
        "pop  %%rax         \n"
        "or   $0x200, %%rax \n"   /* set IF (interrupts enabled) */
        "push %%rax         \n"
        "push %[cs]         \n"   /* CS: user code */
        "push %[rip]        \n"   /* user RIP */
        "iretq              \n"
        :
        : [rip] "r"(rip),
          [rsp] "r"(rsp),
          [cs]  "r"((uint64_t)0x1B),   /* SEG_UCODE|RPL3 = 0x18|3 */
          [ss]  "r"((uint64_t)0x23)    /* SEG_UDATA|RPL3 = 0x20|3 */
        : "rax", "memory"
    );
    __builtin_unreachable();
}

/* ── Fork ────────────────────────────────────────────────────────────── */
int proc_fork(void)
{
    process_t *child = proc_new(current->name);
    if (!child) return -1;

    child->ppid = current->pid;

    /* Clone address space */
    child->aspace = vmm_new_aspace();
    if (!child->aspace) { proc_free(child); return -1; }
    if (vmm_clone(child->aspace, current->aspace) < 0) {
        proc_free(child); return -1;
    }

    /* Copy file descriptors */
    memcpy(child->fds, current->fds, sizeof(current->fds));

    /* Child returns 0 from fork syscall.
     * We set up the kernel stack so that when the child is scheduled,
     * it returns 0 from syscall_dispatch.
     *
     * For a simplified implementation, we save the current user context
     * and the child will return to user space from the syscall return path.
     */
    child->user_rsp = current->user_rsp;
    child->user_rip = current->user_rip;
    child->state    = PROC_READY;

    sched_add(child);
    return child->pid;
}

/* ── Exec ────────────────────────────────────────────────────────────── */
int proc_exec(const char *path, const char **argv, const char **envp)
{
    /* Read file from VFS */
    extern struct vfs_node *vfs_lookup(const char *path);
    extern ssize_t vfs_read(struct vfs_node *, void *, size_t, size_t);

    struct vfs_node *node = vfs_lookup(path);
    if (!node) return -1;

    /* Read entire file */
    size_t size = node->size;
    void *data = vmm_kernel_alloc(size);
    if (!data) return -1;
    if ((size_t)vfs_read(node, data, size, 0) != size) {
        vmm_kernel_free(data, size);
        return -1;
    }

    /* Validate ELF */
    if (!elf_valid(data, size)) {
        vmm_kernel_free(data, size);
        return -1;
    }

    /* Create new address space */
    vmm_aspace_t *new_as = vmm_new_aspace();
    if (!new_as) { vmm_kernel_free(data, size); return -1; }

    elf_load_result_t result;
    if (elf_load(data, size, new_as, 0, &result) < 0) {
        vmm_free_aspace(new_as);
        vmm_kernel_free(data, size);
        return -1;
    }
    vmm_kernel_free(data, size);

    /* If there's a dynamic linker (PT_INTERP), load it */
    uint64_t interp_base = 0;
    if (result.has_interp) {
        struct vfs_node *interp_node = vfs_lookup(result.interp);
        if (interp_node) {
            size_t isz = interp_node->size;
            void *idata = vmm_kernel_alloc(isz);
            if (idata && (size_t)vfs_read(interp_node, idata, isz, 0) == isz) {
                elf_load_result_t iresult;
                interp_base = 0x7F0000000000UL;  /* dynamic linker load base */
                elf_load(idata, isz, new_as, interp_base, &iresult);
                result.entry       = iresult.exec_entry;
                result.interp_base = interp_base;
            }
            if (idata) vmm_kernel_free(idata, isz);
        }
    }

    /* Set up auxv */
    auxv_t auxv[16];
    int na = 0;
    auxv[na++] = (auxv_t){ AT_PHDR,  result.phdr_vaddr };
    auxv[na++] = (auxv_t){ AT_PHENT, result.phdr_ent   };
    auxv[na++] = (auxv_t){ AT_PHNUM, result.phdr_num   };
    auxv[na++] = (auxv_t){ AT_PAGESZ, 4096             };
    auxv[na++] = (auxv_t){ AT_ENTRY, result.exec_entry  };
    auxv[na++] = (auxv_t){ AT_BASE,  interp_base        };
    auxv[na++] = (auxv_t){ AT_UID,   0 };
    auxv[na++] = (auxv_t){ AT_EUID,  0 };
    auxv[na++] = (auxv_t){ AT_GID,   0 };
    auxv[na++] = (auxv_t){ AT_EGID,  0 };
    auxv[na++] = (auxv_t){ AT_SECURE,0 };

    /* Set up user stack */
    uint64_t sp = elf_setup_stack(new_as, 0x7FFFFFFFE000UL,
                                  argv ? (int)({
                                      int c=0; while(argv[c]) c++; c;
                                  }) : 0,
                                  argv, envp, auxv, na);

    /* Compute argc for real */
    int argc = 0;
    if (argv) while (argv[argc]) argc++;
    sp = elf_setup_stack(new_as, 0x7FFFFFFFE000UL, argc, argv, envp, auxv, na);

    /* Replace current address space */
    if (current->aspace) vmm_free_aspace(current->aspace);
    current->aspace = new_as;

    /* Set heap base */
    new_as->brk = new_as->brk_base = 0x1000000UL;  /* 16MB */

    /* Jump to user space */
    jump_to_userspace(result.entry, sp, (uint64_t)new_as->pml4);
    return 0;  /* never reached */
}

void proc_exit(int code)
{
    current->state     = PROC_ZOMBIE;
    current->exit_code = code;

    /* Wake up parent if it's waiting */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proctab_used[i] && proctab[i].state == PROC_WAITING) {
            process_t *p = &proctab[i];
            if (p->wait_pid == -1 || p->wait_pid == current->pid) {
                p->state = PROC_READY;
                break;
            }
        }
    }

    sched_remove(current);
    schedule();
    for (;;) __asm__("hlt");
}

int proc_wait(int pid, int *status)
{
    for (;;) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (!proctab_used[i]) continue;
            process_t *p = &proctab[i];
            if (p->ppid != current->pid) continue;
            if (pid != -1 && p->pid != pid) continue;
            if (p->state == PROC_ZOMBIE) {
                int ret_pid = p->pid;
                if (status) *status = p->exit_code;
                proc_free(p);
                return ret_pid;
            }
        }
        /* Block until a child exits */
        current->state    = PROC_WAITING;
        current->wait_pid = pid;
        schedule();
    }
}

void schedule(void)
{
    if (!run_queue_head) {
        /* No runnable process, idle */
        __asm__ volatile("sti; hlt; cli");
        return;
    }

    /* Find next READY process in the queue */
    process_t *next = run_queue_head;
    process_t *start = next;
    do {
        if (next->state == PROC_READY) break;
        next = next->next;
    } while (next != start);

    if (next->state != PROC_READY) {
        /* No ready process */
        __asm__ volatile("sti; hlt; cli");
        return;
    }

    run_queue_head = next;
    process_t *prev = current;
    current         = next;
    next->state     = PROC_RUNNING;

    /* Update CPU local */
    cpu_local.current    = next;
    cpu_local.kernel_rsp = next->kstack_top;
    tss_set_rsp0(next->kstack_top);

    if (prev && prev != next) {
        /* Context switch: save prev, restore next */
        /* This is a simplified version; full impl needs ctx_switch() */
        /* For our single-threaded shell startup, we just jump */
        paging_load_cr3((pte_t *)next->aspace->pml4);
    }
}

void sched_yield(void) { schedule(); }
