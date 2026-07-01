/*
 * crt0.S - C Runtime startup (statically and dynamically linked)
 *
 * Called by the kernel/dynamic linker when a new process begins.
 * At entry, the stack layout per Linux x86_64 ABI:
 *
 *   [RSP+0]           argc
 *   [RSP+8]           argv[0]
 *   ...
 *   [RSP+8*(argc+1)]  NULL (end of argv)
 *   [RSP+8*(argc+2)]  envp[0]
 *   ...
 *   NULL              (end of envp)
 *   AT_type, AT_val   (auxv entries)
 *   AT_NULL, 0        (end of auxv)
 *
 * We load argc/argv/envp and call main().
 * For dynamically linked programs, the dynamic linker has already
 * run and resolved all symbols before we get here.
 */

/* Use AT&T syntax (GAS default) */

.section .text
.global _start
_start:
    /* Zero the frame pointer to mark end of call chain for debuggers */
    xorq    %rbp, %rbp

    /* Load argc (at [rsp]), argv (rsp+8), envp (rsp + 8*(argc+1) + 8) */
    movq    (%rsp),  %rdi           /* argc */
    leaq    8(%rsp), %rsi           /* argv = &stack[1] */
    leaq    8(%rsi,%rdi,8), %rdx    /* envp = &argv[argc+1] */

    /* Align stack to 16 bytes before any C call */
    andq    $~0xF, %rsp

    /* Reserve 8 bytes so RSP+8 is 16-byte aligned after call */
    pushq   $0

    /* Call main(argc, argv, envp) */
    call    main

    /* Call exit(main's return value) */
    movq    %rax, %rdi
    call    exit

    /* Should never reach here */
.halt:
    hlt
    jmp     .halt
