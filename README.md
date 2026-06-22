If your goal is to **deeply understand GCC internals**, dump files are one of the best learning tools available. This repo includes `test.c` (with `if/else`), example dumps, a patched GCC 11.4 build script, and `visualize_tree_dag.py` for GENERIC tree graphs.

---

# Recommended Learning Path

Follow the same order GCC uses:

```text
C Source
  ↓
Parser
  ↓
GENERIC          (tree IR, whole-function graph)
  ↓
GIMPLE           (flat three-address statements)
  ↓
CFG              (GIMPLE grouped into basic blocks + control-flow edges)
  ↓
SSA              (GIMPLE + CFG + single-assignment renaming + PHI)
  ↓
Optimized GIMPLE (middle-end optimization passes)
  ↓
RTL              (register transfer language; expand → combine → reload → …)
  ↓
Assembly
```

---

# Terminology

| Term | Full name | Meaning |
| ---- | --------- | ------- |
| **bb** | **Basic Block** | A straight-line sequence of statements: one entry, one exit, no branches inside. Shown as `<bb 2>` in tree/RTL dumps. |
| **CFG** | **Control Flow Graph** | A graph whose nodes are basic blocks and whose edges are jumps/branches. A **view on top of GIMPLE**, not a replacement. |
| **SSA** | **Static Single Assignment** | Each SSA name is assigned **once**; reassignments become new versions (`c_1`, `c_5`, …). Built on GIMPLE + CFG. |
| **PHI** | (Greek φ) | A **merge node** at a CFG join point: picks which SSA version to use depending on which predecessor block you came from. |
| **RTL** | **Register Transfer Language** | Back-end IR: `(set (reg ...) (mem ...))`, `(call ...)`, `(jump_insn ...)`. Describes register/memory transfers, not C syntax. |

**Key idea:** GIMPLE, CFG, and SSA are **layers** on the tree middle-end:

```text
GIMPLE  →  what each statement computes
CFG     →  how statements are grouped and how execution flows
SSA     →  unique names + PHI at merge points for dataflow analysis
RTL     →  how values live in registers/stack and how the CPU executes them
```

---

# Dump File Naming

Pattern:

```text
<source>.<NNN><kind-letter>.<pass-suffix>
```

Example: `test.c.245r.expand`

| Part | Meaning |
| ---- | ------- |
| `test.c` | Source file name |
| `245` | **Pass number** in the compiler pipeline (3 digits, varies by GCC version/options) |
| `r` | **Dump kind letter** (see below) |
| `.expand` | **Pass name** / file suffix |

### Kind letters (`t`, `r`, …)

From GCC `dumpfile.c` — the letter after the pass number:

| Letter | Kind | Typical suffixes |
| ------ | ---- | ---------------- |
| **`t`** | **Tree** (middle-end) | `.original`, `.gimple`, `.cfg`, `.ssa`, `.optimized` |
| **`r`** | **RTL** (back-end) | `.expand`, `.combine`, `.reload`, `.sched`, … |
| `l` | Lang (front-end) | language-specific |
| `i` | IPA (inter-procedural) | `.cgraph`, … |

So **`006t.gimple`** = pass #6, **tree** dump, gimple pass.  
**`245r.expand`** = pass #245, **RTL** dump, expand pass.

**Important:** Pass numbers (`006`, `015`, `023`, `244`, `245`) are **not fixed** across GCC versions or `-O` levels. The **suffix** (`.gimple`, `.cfg`, `.expand`) is what identifies the stage.

### Example files (this repo, GCC 11, `-O1`)

| File | Stage |
| ---- | ----- |
| `test.c.005t.original` | GENERIC |
| `test.c.006t.gimple` | GIMPLE |
| `test.c.015t.cfg` | CFG |
| `test.c.023t.ssa` | SSA |
| `test.c.244t.optimized` | Optimized GIMPLE |
| `test.c.245r.expand` | RTL expand |

List all passes:

```bash
gcc -fdump-passes test.c
```

---

# Current `test.c`

```c
void func(int a, int b) {
    int c = 0;
    if (a > b) {
        c = a - b;
    } else {
        c = b - a;
    }
}
void main() {
    func(1, 2);
}
```

Control-flow shape of `func`:

```text
        ┌── bb2: c=0; if (a>b) ──┐
        │                       │
     [true]                  [false]
        │                       │
        ▼                       ▼
     bb3: c=a-b              bb4: c=b-a
        │                       │
        └──────────► bb5 ◄──────┘
                    (void, end)
```

---

# 1. Learn GENERIC

```bash
gcc -fdump-tree-original -c test.c        # C-like pretty print
gcc -fdump-tree-original-raw -c test.c    # @N tree nodes
```

Produces `test.c.005t.original` (both flags write the **same filename**; last flag wins).

Raw dump shows `bind_expr`, `if_stmt`, `modify_expr`, nested expressions. **No `<bb N>`** yet.

```bash
python3 visualize_tree_dag.py test.c.005t.original
./scripts/build-gcc-full-dump.sh && ./scripts/dump-full-dag.sh test.c   # full chain/decl
```

---

# 2. Learn GIMPLE

```bash
gcc -fdump-tree-gimple -c test.c    # → test.c.006t.gimple
```

```text
gimple_bind <
  gimple_assign <integer_cst, c, 0, ...>
  gimple_cond <gt_expr, a, b, <D.1950>, <D.1951>>
  gimple_label <<D.1950>>
  gimple_assign <minus_expr, c, a, b, ...>
  gimple_goto <<D.1952>>
  gimple_label <<D.1951>>
  gimple_assign <minus_expr, c, b, a, ...>
  gimple_label <<D.1952>>
>
```

Flat statements; control flow via **label/goto**, not `<bb N>`.

---

# 3. Learn CFG

```bash
gcc -fdump-tree-cfg -c test.c    # → test.c.015t.cfg
```

```text
;; 2 succs { 3 4 }
;; 3 succs { 5 }
;; 4 succs { 5 }

<bb 2> :
  c = 0;
  if (a > b) goto <bb 3>; else goto <bb 4>;
<bb 3> :
  c = a - b;
  goto <bb 5>;
<bb 4> :
  c = b - a;
<bb 5> :
  ...
```

| | GIMPLE | CFG |
|--|--------|-----|
| Statements | Same | Same |
| Grouping | Linear list in `gimple_bind` | `<bb N>` blocks |
| Branches | `gimple_label` / `gimple_goto` | `goto <bb N>` + `succs` header |

---

# 4. Learn SSA

```bash
gcc -O1 -fdump-tree-ssa -c test.c    # → test.c.023t.ssa
```

Variables get version suffixes:

```text
gimple_assign <integer_cst, c_1, 0, ...>
gimple_assign <minus_expr, c_5, a_2(D), b_3(D), ...>   // then branch
gimple_assign <minus_expr, c_4, b_3(D), a_2(D), ...>   // else branch
```

**PHI** at merge blocks (when needed):

```text
c_N = PHI <c_5(3), c_4(4)>
```

SSA is built on **GIMPLE + CFG** for def-use analysis and optimization.

---

# 5. Optimized GIMPLE

```bash
gcc -O1 -fdump-tree-optimized -c test.c    # → test.c.244t.optimized
```

Output of middle-end optimization passes (after SSA-based opts). Still tree/GIMPLE form, not RTL.

More passes with `-O2`:

```bash
gcc -O2 -fdump-tree-all -c test.c    # → *.ccp1, *.dce1, …
```

| Pass suffix | Meaning |
| ----------- | ------- |
| ccp | Constant propagation |
| dce | Dead code elimination |
| copyprop | Copy propagation |
| fre | Full redundancy elimination |

---

# 6. Learn RTL (expand)

```bash
gcc -O1 -fdump-rtl-expand -c test.c    # → test.c.245r.expand
```

**Expand** lowers optimized GIMPLE to **initial RTL** — the bridge from middle-end to back-end.

File structure:

```text
1. Partition / stack layout for locals
2. "Generating RTL for gimple basic block N"  (maps to CFG bb)
3. try_optimize_cfg / block merging
4. "Full RTL generated for this function"
```

### Example: `func` in `245r.expand`

**Parameters spilled to stack** (x86-64: `a` in `di`, `b` in `si`):

```text
(set (mem ... [a+0]) (reg:SI 5 di [ a ]))
(set (mem ... [b+0]) (reg:SI 4 si [ b ]))
```

**`c = 0`:**

```text
(set (mem ... [c+0]) (const_int 0))
```

**`if (a > b)` — compare + conditional jump:**

```text
(set (reg:CCGC 17 flags) (compare (reg a) (mem b)))
(jump_insn ... (if_then_else (le flags 0) (label_ref 20) (pc)))
```

**Then / else — subtract and store `c`:**

```text
(parallel [(set (reg 85) (minus:SI (reg a) (mem b))) (clobber flags)])
(set (mem c) (reg 85))
```

**`main` — call with arguments:**

```text
(set (reg:SI 5 di) (const_int 1))
(set (reg:SI 4 si) (const_int 2))
(call_insn ... (call ... "func") (use di) (use si))
```

### Common RTL forms

| RTL | Meaning |
| --- | ------- |
| `(insn ... (set A B) ...)` | Assignment / instruction |
| `(reg:SI 5 di [ a ])` | Register (here: 1st arg `a`) |
| `(mem/c:SI ... [1 c+0 ...])` | Stack slot for variable `c` |
| `(compare ...)` | Set condition flags |
| `(jump_insn ... (if_then_else ...))` | Conditional branch |
| `(call_insn ... (call ...))` | Function call |
| `(note ... NOTE_INSN_BASIC_BLOCK)` | Basic block boundary in RTL |

Later RTL passes (`combine`, `reload`, `sched`, …) refine this before assembly. Dump with `-fdump-rtl-all`.

For a **line-by-line walkthrough** of `245r.expand`, GIMPLE→RTL mapping, and RTL→assembly, see [RTL.md](RTL.md).

---

# 7. See Every Pass

```bash
gcc -fdump-passes test.c
```

Shows `tree-gimple`, `tree-cfg`, `tree-ssa`, `tree-optimized`, `rtl-expand`, etc.

---

# 8. GCC Source Files

| Dump token | Source (GCC 11.4) |
| ---------- | ------------------- |
| `gimple_assign` | `gcc/gimple.h`, `gcc/gimple.c` |
| `gimple_phi` | `gcc/tree-ssa*.c` |
| tree nodes | `gcc/tree.h`, `gcc/tree-dump.c` |
| CFG | `gcc/tree-cfg.c` |
| RTL expand | `gcc/expr.c`, `gcc/function.c`, `gcc/print-rtl.c` |

---

# Recommended Sequence

```bash
gcc -O1 \
  -fdump-tree-original-raw \
  -fdump-tree-gimple \
  -fdump-tree-cfg \
  -fdump-tree-ssa \
  -fdump-tree-optimized \
  -fdump-rtl-expand \
  -S test.c
```

Study in order:

```text
005t.original     GENERIC     tree; if_stmt, modify_expr
      ↓
006t.gimple       GIMPLE      flat stmts; label/goto
      ↓
015t.cfg          CFG         <bb N>; succs graph
      ↓
023t.ssa          SSA         c_1, c_4, c_5; PHI at merges
      ↓
244t.optimized    optimized GIMPLE
      ↓
245r.expand       RTL         reg/mem/compare/call/jump
      ↓
test.s            assembly
```

Optional tools:

```bash
python3 visualize_tree_dag.py test.c.005t.original
./scripts/dump-full-dag.sh test.c
```

Pass numbers in filenames may differ; follow the **suffix** and the pipeline order above.
