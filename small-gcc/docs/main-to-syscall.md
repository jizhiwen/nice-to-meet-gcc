# main to syscall call graph

For a tiny binary produced by this project:

1. kernel loads ELF and jumps to `_start`
2. `_start` reads `argc/argv` from stack and calls `main`
3. user code calls `tiny_puts`, `tiny_print_int`, or other helpers
4. helpers eventually call `tiny_write` / `tiny_exit`
5. those wrappers invoke `syscall` directly

Example print path:

`main -> tiny_puts -> tiny_write -> syscall(write)`
