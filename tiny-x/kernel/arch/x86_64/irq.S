/*
 * irq.S - ISR stubs for all 256 interrupt vectors
 *
 * Uses GAS AT&T syntax with .altmacro for iteration.
 *
 * Each stub:
 *   1. Pushes a dummy error code (exceptions without one)
 *   2. Pushes the vector number
 *   3. Saves all general-purpose registers
 *   4. Calls isr_dispatch(interrupt_frame_t *frame)
 *   5. Restores registers and returns via iretq
 *
 * CPU exceptions with hardware error codes: 8,10,11,12,13,14,17,21,29,30
 */

/* Use AT&T syntax (default) for better GAS macro support */

.altmacro

/* ── Save/restore all GP registers (AT&T syntax) ──────────────────────── */
.macro PUSH_ALL
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro POP_ALL
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

/* ── Stub macros ──────────────────────────────────────────────────────── */
/* No error code: push 0, push vector */
.macro ISR_NOERR n
.global isr_stub_\n
isr_stub_\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm

/* Has error code: error already pushed by CPU, just push vector */
.macro ISR_ERR n
.global isr_stub_\n
isr_stub_\n:
    pushq $\n
    jmp isr_common
.endm

/* ── CPU exception stubs (0-31) ───────────────────────────────────────── */
ISR_NOERR 0     /* #DE divide by zero */
ISR_NOERR 1     /* #DB debug */
ISR_NOERR 2     /* NMI */
ISR_NOERR 3     /* #BP breakpoint */
ISR_NOERR 4     /* #OF overflow */
ISR_NOERR 5     /* #BR bound range exceeded */
ISR_NOERR 6     /* #UD invalid opcode */
ISR_NOERR 7     /* #NM device not available */
ISR_ERR   8     /* #DF double fault */
ISR_NOERR 9     /* coprocessor segment overrun */
ISR_ERR   10    /* #TS invalid TSS */
ISR_ERR   11    /* #NP segment not present */
ISR_ERR   12    /* #SS stack segment fault */
ISR_ERR   13    /* #GP general protection */
ISR_ERR   14    /* #PF page fault */
ISR_NOERR 15    /* reserved */
ISR_NOERR 16    /* #MF x87 floating-point */
ISR_ERR   17    /* #AC alignment check */
ISR_NOERR 18    /* #MC machine check */
ISR_NOERR 19    /* #XF SIMD floating-point */
ISR_NOERR 20    /* #VE virtualization */
ISR_ERR   21    /* #CP control protection */
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* ── IRQ stubs (32-47, mapped from PIC) ──────────────────────────────── */
ISR_NOERR 32    /* IRQ 0 - timer */
ISR_NOERR 33    /* IRQ 1 - keyboard */
ISR_NOERR 34    /* IRQ 2 - cascade */
ISR_NOERR 35    /* IRQ 3 - COM2 */
ISR_NOERR 36    /* IRQ 4 - COM1 */
ISR_NOERR 37    /* IRQ 5 */
ISR_NOERR 38    /* IRQ 6 - floppy */
ISR_NOERR 39    /* IRQ 7 */
ISR_NOERR 40    /* IRQ 8 - RTC */
ISR_NOERR 41    /* IRQ 9 */
ISR_NOERR 42    /* IRQ 10 */
ISR_NOERR 43    /* IRQ 11 */
ISR_NOERR 44    /* IRQ 12 - PS/2 mouse */
ISR_NOERR 45    /* IRQ 13 */
ISR_NOERR 46    /* IRQ 14 - ATA primary */
ISR_NOERR 47    /* IRQ 15 - ATA secondary */

/* ── Fill vectors 48-255 with NOERR stubs (using .altmacro iteration) ── */
.set i, 48
.rept 208
ISR_NOERR %i
.set i, i+1
.endr

/* ── Common interrupt handler ─────────────────────────────────────────── */
.global isr_common
isr_common:
    PUSH_ALL

    /* RSP now points to the interrupt_frame_t; pass as first arg */
    movq %rsp, %rdi
    callq isr_dispatch

    POP_ALL

    /* Remove vector + error_code (2 × 8 bytes) */
    addq $16, %rsp

    iretq

/* ── isr_stub_table: array of 256 function pointers ──────────────────── */
.macro STUB_ENTRY n
    .quad isr_stub_\n
.endm

.section .data
.global isr_stub_table
isr_stub_table:
.set i, 0
.rept 256
STUB_ENTRY %i
.set i, i+1
.endr
