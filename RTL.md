# RTL Guide: Line-by-Line Walkthrough of `test.c.245r.expand`

This document uses `test.c.245r.expand` (GCC 11, `-O1`) as a worked example. It explains RTL line by line and covers the full pipeline: **optimized GIMPLE → expand RTL → assembly**.

Related files:

| File | Description |
| --- | --- |
| `test.c` | Example C source |
| `test.c.244t.optimized` | Optimized GIMPLE before expand |
| `test.c.245r.expand` | Initial RTL from the expand pass |
| `test-O0.s` | Assembly from `-O0 -S` (matches expand RTL structure) |

Generate them with:

```bash
gcc -O0 -fdump-rtl-expand -c test.c    # → test.c.245r.expand
gcc -O0 -S -c test.c -o test-O0.s        # assembly for comparison
```

---

## Overview

Example C code:

```c
void func(int a, int b) {
    int c = 0;
    if (a > b) { c = a - b; } else { c = b - a; }
}
void main() { func(1, 2); }
```

Compilation pipeline (simplified):

```text
244t.optimized (GIMPLE + CFG)
        ↓  pass_expand (cfgexpand.c)
245r.expand    (initial RTL)
        ↓  combine / reload / sched / …
pass_final     (final.c)
        ↓
test.s         (assembly)
```

Optimized GIMPLE **before** expand (`244t.optimized`):

```text
<bb 2> :
  c_1 = 0;
  if (a_2(D) > b_3(D))
    goto <bb 3>; else goto <bb 4>;

<bb 3> :
  c_5 = a_2(D) - b_3(D);
  goto <bb 5>;

<bb 4> :
  c_4 = b_3(D) - a_2(D);

<bb 5> :
  return;
```

The **expand pass** (`pass_expand` in `gcc/cfgexpand.c`) does roughly:

1. `rewrite_out_of_ssa` — turn SSA names (`a_2`, `c_5`) back into ordinary variables
2. `expand_function_start` — function prologue and parameter setup
3. For each basic block, call `expand_gimple_basic_block` — **one GIMPLE statement → several RTL insns**
4. `try_optimize_cfg` — merge/simplify the RTL-level CFG
5. Write the dump → `245r.expand`

Dozens of RTL passes follow (see `gcc/passes.def`); the last one, `pass_final` (`gcc/final.c`), prints assembly.

---

## How to Read One RTL Line

Example (lines 36–39):

```text
(insn 2 5 3 2 (set ...) "test.c":2:25 -1 (nil))
```

| Field | Meaning |
| --- | --- |
| `insn` | Ordinary instruction (also `jump_insn`, `call_insn`, `note`, …) |
| `2` | UID of this insn |
| `5` | UID of the previous insn |
| `3` | UID of the next insn |
| `2` | Basic block index (bb 2) |
| `(set A B)` | **Core meaning**: `A = B` |
| `"test.c":2:25` | Source location: line 2, column 25 |
| `(nil)` | No extra attributes |

**Reading tip:** Find `(set/call/compare)` first, then `"test.c":line`. Ignore UID/chain fields at first.

---

## Building Blocks That Appear Everywhere

### 1. Stack addresses: `virtual-stack-vars`

```text
(plus:DI (reg/f:DI 77 virtual-stack-vars) (const_int -20))
```

- `virtual-stack-vars` (reg 77) = GCC's virtual base for stack locals
- `const_int -20` = offset
- Together ≈ **address of variable `a` on the stack**
- Final assembly (`-O0`): `movl %edi, -20(%rbp)`

### 2. Stack layout (matches `-O0` assembly)

| Variable | RTL offset | Assembly |
| --- | --- | --- |
| `c` | `-4` | `-4(%rbp)` |
| `a` | `-20` | `-20(%rbp)` |
| `b` | `-24` | `-24(%rbp)` |

### 3. x86-64 argument registers

| RTL register | Physical reg | Role |
| --- | --- | --- |
| `(reg:SI 5 di)` | `%edi` | 1st integer argument `a` |
| `(reg:SI 4 si)` | `%esi` | 2nd integer argument `b` |

### 4. `(mem/c:SI ... [1 a+0 S4 A32])`

- `mem` = memory operand
- `/c:SI` = 32-bit signed integer with cached alias info
- `[1 a+0 S4 A32]` = GCC annotation: stack slot for `a`, 4 bytes

### 5. Common RTL forms (quick reference)

| RTL | Meaning |
| --- | --- |
| `(insn ... (set A B) ...)` | Assignment / ordinary insn |
| `(reg:SI 5 di [ a ])` | Register (`[ a ]` is a GCC comment) |
| `(mem/c:SI ... [1 c+0 ...])` | Stack slot for `c` |
| `(compare ...)` | Compare; sets condition codes |
| `(jump_insn ... (if_then_else ...))` | Conditional branch |
| `(call_insn ... (call ...))` | Function call |
| `(note ... NOTE_INSN_BASIC_BLOCK)` | Basic block boundary |
| `(parallel [... (clobber ...)])` | Parallel effects (e.g. sub also clobbers flags) |

---

## `func`: Line-by-Line (lines 1–114)   [TODO 0622 13:38]

### Lines 1–5: Function header + stack partition

```text
;; Function func (func, funcdef_no=0, ...)

Partition 0: size 4 align 4
    c_1
```

- Start of the `func` dump
- SSA partition: variable `c` uses a **4-byte, 4-byte-aligned** stack slot (`c_1` is the SSA version)

### Lines 7–13: Which GIMPLE basic blocks are being expanded

```text
;; Generating RTL for gimple basic block 2
;; Generating RTL for gimple basic block 3
;; Generating RTL for gimple basic block 4
;; Generating RTL for gimple basic block 5
```

GCC calls `expand_gimple_basic_block` and translates GIMPLE bb by bb:

| GIMPLE bb | Contents |
| --- | --- |
| bb 2 | `c=0` + `if (a>b)` |
| bb 3 | `c = a-b; goto bb5` |
| bb 4 | `c = b-a` |
| bb 5 | `return` |

### Lines 16–27: RTL-level CFG optimization

```text
try_optimize_cfg iteration 1
Merging block 3 into block 2...
Redirecting jump 18 from 6 to 7.
Merging block 6 into block 5...
try_optimize_cfg iteration 2
```

After RTL is generated, empty/redundant blocks are merged and jumps retargeted. Final RTL bb numbers (`[bb 4]`, `[bb 5]`, `[bb 7]`) **do not always match** GIMPLE bb numbers, but control flow is the same.

### Lines 31–33: Start of final RTL listing

```text
;;
;; Full RTL generated for this function:
;;
```

Below is the full insn chain after CFG merging.

### Line 34: Placeholder note [TODO 0625 20:51]

```text
(note 1 0 5 NOTE_INSN_DELETED)
```

Placeholder from `emit_note(NOTE_INSN_DELETED)` at expand start — keeps the insn list non-empty (GCC internal convention).

### Line 35: Basic block 2 begins

```text
(note 5 1 2 2 [bb 2] NOTE_INSN_BASIC_BLOCK)
```

Marks **entry of bb 2**, corresponding to GIMPLE `<bb 2>`.

### Lines 36–39: Spill parameter `a` to stack

```text
(insn 2 ... (set (mem ... -20 ... [1 a+0 ...])
        (reg:SI 5 di [ a ])) "test.c":2:25 ...)
```

**Meaning:** `stack[a] = di` (1st argument from `%edi` stored on stack)

- **From GIMPLE:** parameter `a_2(D)`
- **Expand mechanism:** `assign_parms` / variable expansion

**Assembly** (`-O0`):

```asm
movl    %edi, -20(%rbp)
```

### Lines 40–43: Spill parameter `b` to stack

```text
(insn 3 ... (set (mem ... -24 ... [1 b+0 ...])
        (reg:SI 4 si [ b ])) "test.c":2:25 ...)
```

**Meaning:** `stack[b] = si`

**Assembly:**

```asm
movl    %esi, -24(%rbp)
```

### Line 44: Function body begins

```text
(note 4 3 7 2 NOTE_INSN_FUNCTION_BEG)
```

`NOTE_INSN_FUNCTION_BEG` marks the start of the function body; parameter spills are **before** this (prologue).

### Lines 45–48: `c = 0`

```text
(insn 7 ... (set (mem ... -4 ... [1 c+0 ...])
        (const_int 0)) "test.c":3:6 ...)
```

**Meaning:** `stack[c] = 0`

- **GIMPLE:** `c_1 = 0;` (bb 2)
- **Expand:** `expand_gimple_assign`

**Assembly:**

```asm
movl    $0, -4(%rbp)
```

### Lines 49–52: Load `a` into a temp register

```text
(insn 8 ... (set (reg:SI 82)
        (mem ... -20 ... [1 a+0 ...])) "test.c":4:5 ...)
```

**Meaning:** `reg82 = stack[a]`

- **GIMPLE:** `if (a_2(D) > b_3(D))` needs to read `a`

**Assembly:**

```asm
movl    -20(%rbp), %eax
```

### Lines 53–57: Compare `a` and `b`

```text
(insn 9 ... (set (reg:CCGC 17 flags)
        (compare:CCGC (reg:SI 82)
            (mem ... -24 ... [1 b+0 ...]))) "test.c":4:5 ...)
```

**Meaning:** `flags = compare(reg82, stack[b])` — like x86 `cmp`.

- **GIMPLE:** `if (a_2(D) > b_3(D))` (`gt_expr`)
- **Expand:** `expand_gimple_cond` emits `(compare ...)`

**Assembly:**

```asm
cmpl    -24(%rbp), %eax
```

### Lines 58–64: Conditional branch

```text
(jump_insn 10 ... (set (pc)
        (if_then_else (le (reg:CCGC 17 flags) (const_int 0))
            (label_ref 20)
            (pc))) "test.c":4:5 ...
 -> 20)
```

**Meaning:**

```text
if (flags means a <= b)
    pc = label 20    // else branch
else
    pc = fall through // then branch
```

GIMPLE uses `a > b` to jump to then; RTL writes **`le` (≤) to jump to else** — logically equivalent, inverted condition.

- `label_ref 20` → line 91 `(code_label 20 ...)` (else entry)
- `-> 20` = jump target

**GIMPLE:**

```text
if (a > b) goto <bb 3>; else goto <bb 4>;
```

**Assembly:**

```asm
jle     .L2
```

### Line 65: Basic block 4 begins (then branch)

```text
(note 11 10 15 4 [bb 4] NOTE_INSN_BASIC_BLOCK)
```

Corresponds to GIMPLE `<bb 3>`: `c = a - b` (renumbered to bb 4 after CFG merge).

### Lines 66–69: Then branch loads `a`

```text
(insn 15 ... (set (reg:SI 86) (mem ... [a])) "test.c":5:5 ...)
```

**Meaning:** `reg86 = stack[a]` (operand for subtraction)

### Lines 70–81: Then branch `c = a - b`

```text
(insn 16 ... (parallel [
            (set (reg:SI 85) (minus:SI (reg:SI 86) (mem ... [b])))
            (clobber (reg:CC 17 flags))
        ]) "test.c":5:5 ...
     (expr_list:REG_EQUAL (minus:SI (mem a) (mem b)) ...))
```

**Meaning:**

- `parallel` = simultaneous: `reg85 = reg86 - stack[b]`, and `clobber flags`
- `expr_list:REG_EQUAL` = optimization hint: `reg85 ≡ mem[a] - mem[b]`

**GIMPLE:** `c_5 = a_2(D) - b_3(D);` (bb 3)

**Assembly:**

```asm
movl    -20(%rbp), %eax
subl    -24(%rbp), %eax
```

### Lines 82–85: Store result into `c`

```text
(insn 17 ... (set (mem ... [c]) (reg:SI 85)) "test.c":5:5 ...)
```

**Meaning:** `stack[c] = reg85`

**Assembly:**

```asm
movl    %eax, -4(%rbp)
```

### Lines 86–89: Then branch jumps to merge point

```text
(jump_insn 18 ... (set (pc) (label_ref:DI 30)) 807 {jump}
 -> 30)
```

**Meaning:** unconditional jump to label 30 (merge before return)

**GIMPLE:** `goto <bb 5>;` (end of bb 3)

**Assembly:**

```asm
jmp     .L4
```

### Line 90: Barrier

```text
(barrier 19 18 20)
```

Prevents insns from being reordered/merged across this point; keeps then/else boundaries clear.

### Line 91: Else branch label

```text
(code_label 20 19 21 5 2 (nil) [1 uses])
```

- `code_label 20` = else entry
- Referenced by `(label_ref 20)` in jump_insn 10

**Assembly:**

```asm
.L2:
```

### Line 92: Basic block 5 begins (else branch)

```text
(note 21 20 25 5 [bb 5] NOTE_INSN_BASIC_BLOCK)
```

Corresponds to GIMPLE `<bb 4>`: `c = b - a`

### Lines 93–96: Else branch loads `b`

```text
(insn 25 ... (set (reg:SI 90) (mem ... [b])) "test.c":7:5 ...)
```

**Meaning:** `reg90 = stack[b]`

### Lines 97–108: Else branch `c = b - a`

```text
(insn 26 ... (parallel [
            (set (reg:SI 89) (minus:SI (reg:SI 90) (mem ... [a])))
            (clobber (reg:CC 17 flags))
        ]) "test.c":7:5 ...)
```

**Meaning:** `reg89 = reg90 - stack[a]` → `b - a`

**GIMPLE:** `c_4 = b_3(D) - a_2(D);` (bb 4)

**Assembly:**

```asm
movl    -24(%rbp), %eax
subl    -20(%rbp), %eax
movl    %eax, -4(%rbp)
```

### Lines 109–112: Store `c` (else)

```text
(insn 27 ... (set (mem ... [c]) (reg:SI 89)) "test.c":7:5 ...)
```

Same as then branch: store result to `stack[c]`.

### Lines 113–114: Merge point + exit bb

```text
(code_label 30 27 31 7 1 (nil) [1 uses])
(note 31 30 0 7 [bb 7] NOTE_INSN_BASIC_BLOCK)
```

- `label 30` = target of then's `jmp`; else falls through here
- `[bb 7]` = function exit (GIMPLE `<bb 5> return`)

The expand dump **ends here** without explicit `return` RTL; the epilogue is generated in `expand_function_end`.

---

## `main`: Line-by-Line (lines 116–154)

### Lines 116–119

```text
;; Function main (...)
;; Generating RTL for gimple basic block 2
```

`main` has a single bb 2: `func(1,2); return;`

### Lines 122–132: CFG merging

```text
Merging block 3 into block 2...
Merging block 4 into block 2...
```

Multiple bb's merged into one; final output has only `[bb 2]`.

### Lines 139–141

```text
(note 1 0 3 NOTE_INSN_DELETED)
(note 3 1 2 2 [bb 2] NOTE_INSN_BASIC_BLOCK)
(note 2 3 5 2 NOTE_INSN_FUNCTION_BEG)
```

bb 2 entry + function body start.

### Lines 142–147: Set up call arguments

```text
(insn 5 ... (set (reg:SI 4 si) (const_int 2)) "test.c":11:2 ...)
(insn 6 ... (set (reg:SI 5 di) (const_int 1)) "test.c":11:2 ...)
```

x86-64 System V ABI:

- 2nd argument `2` → `%esi` (si)
- 1st argument `1` → `%edi` (di)

**GIMPLE:** `func (1, 2);`

**Assembly:**

```asm
movl    $2, %esi
movl    $1, %edi
```

### Lines 148–153: Call `func`

```text
(call_insn 7 ... (call (mem:QI (symbol_ref:DI ("func") ...))
        (const_int 0)) "test.c":11:2 ...
    (expr_list:SI (use (reg:SI 5 di))
        (expr_list:SI (use (reg:SI 4 si)) (nil))))
```

| Part | Meaning |
| --- | --- |
| `call_insn` | Call instruction |
| `(call (mem ... ("func")))` | Call symbol `func` |
| `(const_int 0)` | Stack argument bytes (0 when using registers) |
| `(use (reg:SI 5 di))` | `di` is live before the call |
| `(use (reg:SI 4 si))` | `si` is live before the call |

**Expand:** `expand_gimple_call`

**Assembly:**

```asm
call    func
```

---

## Optimized GIMPLE → RTL: Statement Mapping

| Optimized GIMPLE | Expand RTL (lines) | Mechanism |
| --- | --- | --- |
| `c_1 = 0` (bb2) | insn 7 (45–48) | `expand_gimple_assign` |
| `if (a > b)` (bb2) | insn 8–9 + jump_insn 10 (49–64) | `expand_gimple_cond` |
| `c_5 = a - b` (bb3) | insn 15–17 (66–85) | assign → `minus:SI` |
| `goto bb5` (bb3) | jump_insn 18 (86–89) | edge → `label_ref 30` |
| `c_4 = b - a` (bb4) | insn 25–27 (93–112) | assign → `minus:SI` |
| `return` (bb5) | label 30 + bb 7 (113–114) | epilogue at end of expand |
| `func(1,2)` (main) | insn 5–7 + call_insn 7 (142–153) | `expand_gimple_call` |

Simplified expand pseudocode:

```c
// pass_expand::execute (cfgexpand.c)
rewrite_out_of_ssa();
expand_function_start();
for (bb : all_bbs)
    expand_gimple_basic_block(bb);
try_optimize_cfg();
expand_function_end();
```

---

## RTL → Assembly

### 1. Major passes after expand (`gcc/passes.def`)

```text
pass_expand                         ← 245r.expand
  pass_instantiate_virtual_regs     ← virtual-stack-vars → real fp/sp
  pass_jump, pass_cse, pass_combine ...
  pass_ira, pass_reload             ← register allocation
  pass_sched                        ← instruction scheduling
  pass_peephole2                    ← peephole optimization
  pass_final                        ← emit .s
```

### 2. RTL shape at each stage

| Stage | Pseudo registers | Stack addresses | Branches |
| --- | --- | --- | --- |
| **expand** | `reg 82`, `reg 85`, … | `virtual-stack-vars -20` | `(jump_insn ... (if_then_else ...))` |
| **after reload** | `%eax`, `%ebx`, … | `(mem (plus (reg:SP) ...))` | operands materialized |
| **final** | pure assembly | `-20(%rbp)` | `jle`, `jmp` |

### 3. Expand RTL → `-O0` assembly mapping

**func:**

| Expand RTL | Assembly |
| --- | --- |
| spill `a`,`b` (insn 2–3) | `movl %edi,-20(%rbp)` / `movl %esi,-24(%rbp)` |
| `c=0` (insn 7) | `movl $0,-4(%rbp)` |
| compare + jump (insn 8–10) | `movl -20(%rbp),%eax` / `cmpl -24(%rbp),%eax` / `jle .L2` |
| then `a-b` (insn 15–17) | `movl -20(%rbp),%eax` / `subl -24(%rbp),%eax` / `movl %eax,-4(%rbp)` |
| jmp (insn 18) | `jmp .L4` |
| else `b-a` (insn 25–27) | `.L2:` / `movl -24(%rbp),%eax` / `subl -20(%rbp),%eax` / `movl %eax,-4(%rbp)` |
| merge `.L4` | `nop` |

**main:**

| Expand RTL | Assembly |
| --- | --- |
| insn 5–6 | `movl $2,%esi` / `movl $1,%edi` |
| call_insn 7 | `call func` |

### 4. What `pass_final` does

In `gcc/final.c`, `final_scan_insn`:

1. Walks each RTL insn
2. Looks up target insn patterns (x86 `*cmp`, `*sub`, `*call`, …)
3. Matches `(set (reg) (minus ...))` to templates like `"subl %3, %0"`
4. Prints to the `.s` file

---

## Is ARM RTL the Same as x86?

**Same syntax, different content.**

| Layer | Same? | Notes |
| --- | --- | --- |
| RTL **language/format** | Yes | All use `(set ...)`, `(reg ...)`, `(mem ...)` |
| **GIMPLE/SSA input** | Very similar | Same C source, same `-O` level |
| **expand RTL** | No | Register names, calling convention, compare/branch patterns differ |
| **Later passes** | Even more different | Closer to real machine instructions |

x86-64 uses `di`/`si` and `flags`; AArch64 typically uses `r0`/`r1` with different compare/branch patterns.

Compare dumps:

```bash
gcc -O1 -fdump-rtl-expand -c test.c
aarch64-linux-gnu-gcc -O1 -fdump-rtl-expand -c test.c
```

---

## Note: `-O1` Assembly vs Expand Dump

`245r.expand` at `-O1` **still contains full logic**, but final assembly may be nearly empty:

```asm
func:
    ret
main:
    ret
```

Why:

1. In optimized GIMPLE, `c` is **never used** (dead store)
2. Expand still emits RTL (full DCE has not run yet)
3. Later RTL passes see `func` has no side effects → remove it
4. `main`'s `call func` is removed too

**Recommendation:**

- Learn RTL → read `245r.expand`
- Learn RTL-to-assembly mapping → use `-O0 -S`

```bash
gcc -O0 -S -c test.c -o test-O0.s
gcc -O1 -fdump-rtl-expand -c test.c
```

---

## GIMPLE bb vs Expand RTL Control Flow

```text
GIMPLE (244t.optimized)          expand RTL (245r.expand)
─────────────────────────        ─────────────────────────
<bb 2>  c=0; if(a>b)             [bb 2] spill a,b; c=0; compare; jump
   ├─ then → <bb 3>                 ├─ then → [bb 4]  c=a-b; jmp label30
   └─ else → <bb 4>                 └─ else → [bb 5]  c=b-a
<bb 3>  c=a-b; goto bb5               └─ merge → label30 [bb 7]
<bb 4>  c=b-a
<bb 5>  return
```

---

## Related GCC Source

| Topic | File |
| --- | --- |
| Expand pass entry | `gcc/cfgexpand.c` — `pass_expand::execute` |
| Per-bb expansion | `gcc/cfgexpand.c` — `expand_gimple_basic_block` |
| Cond/assign/call expansion | `gcc/cfgexpand.c` — `expand_gimple_cond`, `expand_gimple_stmt` |
| Pass order | `gcc/passes.def` |
| Assembly output | `gcc/final.c` — `final_scan_insn` |
| RTL printing | `gcc/print-rtl.c` |

---

## Further Reading

```bash
# RTL dumps after expand
gcc -O0 -fdump-rtl-all -c test.c

# List all passes
gcc -fdump-passes test.c

# reload and sched only
gcc -O0 -fdump-rtl-reload -fdump-rtl-sched -c test.c
```

For GENERIC, GIMPLE, and SSA, see [README.md](README.md).
