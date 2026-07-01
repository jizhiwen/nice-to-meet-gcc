/*
 * syscall.S - SYSCALL/SYSRET entry point
 *
 * x86_64 SYSCALL convention (Linux ABI):
 *   rax = syscall number
 *   rdi = arg1,  rsi = arg2,  rdx = arg3
 *   r10 = arg4,  r8  = arg5,  r9  = arg6
 *   rcx = caller's RIP (saved by SYSCALL instruction)
 *   r11 = caller's RFLAGS
 *
 * On entry to syscall_entry:
 *   - CPU is in ring-0
 *   - RSP still points to user stack
 *   - RIP is saved in RCX, RFLAGS in R11
 *
 * We switch to kernel stack, save state, dispatch to C handler.
 */

.intel_syntax noprefix

/* MSR indices */
#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_FMASK       0xC0000084

/* Offsets into task_state_t (process.h) for kernel stack */
/* These must match struct task_state layout exactly */
#define TASK_KSTACK_OFF 8   /* offset of kstack_top in current task */

.section .text
.global syscall_init
syscall_init:
    /* Enable SYSCALL/SYSRET in EFER.SCE */
    mov ecx, MSR_EFER
    rdmsr
    or  eax, 1          /* SCE bit */
    wrmsr

    /*
     * STAR MSR layout:
     *   [47:32] = kernel CS (SYSCALL loads CS = STAR[47:32])
     *   [63:48] = user CS   (SYSRET loads CS = STAR[63:48]+16, SS = STAR[63:48]+8)
     * Set:
     *   kernel CS = 0x08, kernel SS = 0x10
     *   user   CS = 0x18 (SYSRET adds 16 for 64-bit → 0x28... use 0x10 trick)
     * Linux uses: STAR[47:32]=0x10 (SYSRET: user CS=0x10+16=0x28... )
     * Actually: SYSRET sets CS=STAR[63:48]+16, SS=STAR[63:48]+8
     * So for user CS=0x28 (SEG_UCODE+RPL3 without RPL), SS=0x20+8=0x28...
     * Simpler: set [47:32]=0x08, [63:48]=0x10
     *   SYSCALL: CS=0x08, SS=0x10 (kernel)
     *   SYSRET:  CS=0x10+16=0x28 (SEG_UCODE|RPL3 needs 0x18|3=0x1B hmm)
     *
     * Standard Linux STAR value: kernel=0x10, user=0x18
     *   SYSCALL: CS=0x10, SS=0x18 ... no that's wrong too.
     *
     * Correct: 
     *   bits[47:32] = kernel code selector = 0x08
     *   bits[63:48] = user code selector - 16 = 0x18 - 16 = 0x08 ... 
     *
     * Actually the correct Linux approach:
     *   STAR[47:32] = 0x08  → SYSCALL uses CS=0x08, SS=0x08+8=0x10
     *   STAR[63:48] = 0x1B  → SYSRET uses CS=0x1B+1=0x1C?? No.
     *
     * Let me use the actual Linux approach:
     *   GDT layout: null(0), kcode(8), kdata(16), ucode(24|3=27), udata(32|3=35)
     *   STAR[63:48]=0x10: SYSRET CS=0x10+16=0x28=40, SS=0x10+8=0x18=24
     *   That doesn't match either.
     *
     * The rule is:
     *   SYSRET to 64-bit: CS = STAR[63:48] + 16, SS = STAR[63:48] + 8
     *   For user CS=0x18|3=0x1B and SS=0x20|3=0x23:
     *     STAR[63:48] = 0x1B - 16 = 0x0B ? No, must be 8-aligned.
     *
     * Standard answer: GDT must be arranged so that:
     *   user_data is right after user_code (index 4, index 3 in our table)
     *   STAR[63:48] = user_code_base - 16
     *   With our layout: null=0, kcode=8, kdata=16, ucode=24, udata=32
     *   We want user CS=0x18+3=0x1B after SYSRET: STAR[63:48] = 0x1B-16 = hmm
     *
     * Simplest correct setup for our GDT:
     *   null(0x00), kcode(0x08), kdata(0x10), ucode(0x18), udata(0x20)
     *   STAR[47:32] = 0x08 (kernel CS for SYSCALL)
     *   STAR[63:48] = 0x13 so SYSRET uses CS=0x13+16=0x23=35?
     *
     * I'll use the Linux approach with GDT:
     *   0x00=null, 0x08=kcode, 0x10=kdata, 0x18=ucode, 0x20=udata
     *   STAR = (0x0013 << 48) | (0x0008 << 32)
     *   This gives: SYSCALL CS=0x08,SS=0x10; SYSRET64 CS=0x13+16=0x23,SS=0x13+8=0x1B
     *   0x23|3=? no, SYSRET doesn't OR with RPL
     *
     * Actually SYSRET sets CS to STAR[63:48]+16 with RPL=3 forced by CPU,
     * and SS to STAR[63:48]+8 with RPL=3.
     * So with STAR[63:48]=0x10: CS=0x20|3=0x23, SS=0x18|3=0x1B
     * But our ucode=0x18 needs selector 0x18, and with RPL=3 it's 0x1B=27.
     * Hmm, that doesn't work.
     *
     * CORRECT: With STAR[63:48]=0x08: CS=0x18|3=0x1B, SS=0x10|3=0x13 - but
     * SS=0x13 would be kdata|RPL3 which works (selector value, not RPL in GDT).
     * Actually the RPL field in the selector just affects privilege checks;
     * the GDT index is (selector >> 3). So 0x1B >> 3 = 3 → ucode. Correct!
     * And 0x13 >> 3 = 2 → kdata, with RPL=3. This is what Linux uses!
     *
     * Final: STAR[47:32]=0x08, STAR[63:48]=0x08 also? No...
     * 
     * Use: (0x0010ULL << 48) | (0x0008ULL << 32)
     *   SYSCALL: CS=0x08, SS=0x10
     *   SYSRET64: CS=(0x10+16)|3=0x23, SS=(0x10+8)|3=0x1B
     *   0x23>>3=4=udata?? and 0x1B>>3=3=ucode??
     *   That's backwards!
     *
     * The correct magic with our GDT (0=null,8=kcode,16=kdata,24=ucode,32=udata):
     *   We need SYSRET to load CS=0x1B (ucode|RPL3) and SS=0x23 (udata|RPL3)
     *   From rule: CS = STAR[63:48]+16 (with RPL=3 added by CPU)
     *              SS = STAR[63:48]+8  (with RPL=3 added by CPU)
     *   CS = (STAR[63:48]+16) | 3 = 0x1B → STAR[63:48]+16 = 0x18 → STAR[63:48]=0x08
     *   SS = (STAR[63:48]+8)  | 3 = 0x23?? 0x08+8=0x10 | 3 = 0x13 ≠ 0x23
     *
     * The SS formula with STAR[63:48]=0x08: SS=(0x08+8)|3=0x10|3=0x13
     * 0x13>>3=2=kdata. That means SS uses kdata descriptor (udata would be index 4=0x20).
     *
     * This is a known issue: SYSCALL/SYSRET requires a specific GDT layout.
     * Linux uses: null,kcode,kdata,udata,ucode (note: udata BEFORE ucode)
     * With that: 0=null, 8=kcode, 16=kdata, 24=udata, 32=ucode
     *   STAR[63:48]=0x10: CS=(0x10+16)|3=0x23=ucode|RPL3 ✓
     *                     SS=(0x10+8)|3=0x1B=udata|RPL3 ✓
     *   STAR[47:32]=0x08: CS=0x08=kcode ✓, SS=0x10=kdata ✓
     */
    mov ecx, MSR_STAR
    rdmsr
    /* STAR[47:32] = 0x0008 (kernel CS), STAR[63:48] = 0x0010 */
    mov edx, (0x0010 << 16) | 0x0008
    xor eax, eax
    wrmsr

    /* LSTAR = address of syscall entry handler */
    mov ecx, MSR_LSTAR
    lea rax, [rip + syscall_entry]
    mov rdx, rax
    shr rdx, 32
    wrmsr

    /* FMASK = mask applied to RFLAGS on SYSCALL (clear IF to disable interrupts) */
    mov ecx, MSR_FMASK
    mov eax, 0x200          /* clear IF bit in RFLAGS */
    xor edx, edx
    wrmsr

    ret

/*
 * syscall_entry - called by SYSCALL instruction
 *
 * On entry:
 *   RSP = user stack
 *   RCX = user RIP
 *   R11 = user RFLAGS
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = arguments
 */
.global syscall_entry
syscall_entry:
    /* Swap to kernel stack using per-CPU current task pointer */
    /* We save user RSP in a scratch location, load kernel RSP */
    swapgs                      /* FS/GS base: kernel stores current task ptr in GS */

    /* Save user stack pointer */
    mov  [gs:0x10], rsp         /* gs:0x10 = cpu_local.user_rsp scratch */
    mov  rsp, [gs:0x08]         /* gs:0x08 = cpu_local.kernel_rsp (tss.rsp0) */

    /* Build a syscall frame on kernel stack */
    push [gs:0x10]              /* user RSP */
    push r11                    /* user RFLAGS */
    push rcx                    /* user RIP */

    /* Save callee-saved regs (syscall may use r10 as 4th arg) */
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    /* Move r10 into rcx for C calling convention (arg4) */
    mov rcx, r10

    /* Dispatch: syscall_dispatch(nr, a1, a2, a3, a4, a5, a6) */
    /* rax=nr, rdi=a1, rsi=a2, rdx=a3, rcx(r10)=a4, r8=a5, r9=a6 */
    mov rdi, rax    /* nr is now first arg... wait, syscall_dispatch signature:
                       int64_t syscall_dispatch(uint64_t nr, ...) uses rdi first.
                       But Linux convention has nr in rax, args in rdi,rsi,rdx,r10...
                       Our C function: syscall_dispatch(nr,a1,a2,a3,a4,a5,a6)
                       so shift args: */
    push r9
    push r8
    push rcx        /* was r10, now arg4 position */
    push rdx
    push rsi
    push rdi
    push rax        /* syscall number */

    /* call dispatch(nr=rdi, a1=rsi, a2=rdx, a3=rcx, a4=r8, a5=r9, a6=stack) */
    /* Actually let's just pass them properly */
    pop rdi         /* nr */
    pop rsi         /* a1 (original rdi) */
    pop rdx         /* a2 (original rsi) */
    pop rcx         /* a3 (original rdx) */
    pop r8          /* a4 (original r10) */
    pop r9          /* a5 (original r8) */
    /* a6 (original r9) is on stack, but we ignore it for now */
    add rsp, 8

    call syscall_dispatch

    /* Return value in rax */

    /* Restore saved regs */
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    /* Restore user context */
    pop rcx     /* user RIP → rcx for SYSRET */
    pop r11     /* user RFLAGS */
    pop rsp     /* user RSP */

    swapgs
    sysretq
