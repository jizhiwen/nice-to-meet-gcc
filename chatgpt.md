A useful way to think about GCC RTL is: **it is not a “language grammar” in the usual sense**, but a **prefix-notation tree format describing machine-like operations**.

Once you see the structure, it becomes mechanical.

---

# 1. The core idea of RTL grammar

RTL is basically:

> **(operation operand1 operand2 operand3 ...)**

Everything is an S-expression (LISP-like tree).

So:

```text
(set A B)
```

means:

> A = B

---

# 2. The real “grammar rule”

A simplified formal view:

```text
RTX := (CODE MODE OPERANDS...)
     | (CODE OPERANDS...)
```

Where:

* **CODE** = operation type (`set`, `plus`, `mem`, `reg`, `call`, etc.)
* **MODE** = data type (`SI`, `DI`, `SF` …)
* **OPERANDS** = nested RTX expressions

So RTL is:

> a recursive tree: every node is either an operator or a leaf.

---

# 3. The 3 most important node types

## (A) register nodes

```text
(reg:SI 5 di)
```

Meaning:

* `reg` = register
* `SI` = 32-bit integer
* `5 di` = GCC internal register number mapping to `%edi`

👉 Interpretation:

> a CPU register holding a value

---

## (B) memory nodes

```text
(mem/c:SI (plus:DI (reg/f ...) (const_int -20)))
```

Break it:

* `mem` → memory access
* `/c` → cached alias info
* `SI` → 32-bit integer
* inside → address computation

So:

> load/store from memory at computed address

---

## (C) expressions (ops)

```text
(plus:DI X Y)
```

Meaning:

> X + Y (64-bit arithmetic)

Other examples:

| RTL      | Meaning     |
| -------- | ----------- |
| `plus`   | addition    |
| `minus`  | subtraction |
| `mult`   | multiply    |
| `ashift` | shift left  |
| `ior`    | bitwise OR  |

---

# 4. The MOST important pattern: assignment

```text
(set A B)
```

This is the core instruction meaning:

> A = B

But A and B can be anything:

### Example 1 (register move)

```text
(set (reg:SI 5 di) (reg:SI 4 si))
```

→ `%edi = %esi`

---

### Example 2 (store to stack)

```text
(set (mem:SI addr) (reg:SI 5 di))
```

→ store register into memory

---

### Example 3 (arithmetic)

```text
(set (reg:SI 0 eax)
     (plus:SI (reg:SI 5 di)
              (reg:SI 4 si)))
```

→ `eax = edi + esi`

---

# 5. Why everything looks nested

Because RTL is:

> a tree, not a flat instruction

So instead of writing:

```c
a = b + c;
```

RTL writes:

```text
(set (reg a)
     (plus (reg b)
           (reg c)))
```

---

# 6. Your stack example decoded

```text
(plus:DI
   (reg/f:DI 77 virtual-stack-vars)
   (const_int -20))
```

Meaning:

* base pointer = virtual stack base
* offset = -20

So:

> address = stack_base - 20

Then wrapped in:

```text
(mem ...)
```

means:

> memory at that address

---

# 7. Common “instruction wrapper” level

RTL instructions are wrapped like:

```text
(insn UID PREV NEXT BB (set ...) LOC ...)
```

Meaning:

* `insn` = instruction node
* UID = unique id
* PREV/NEXT = linked list
* BB = basic block id
* `(set ...)` = actual operation

So:

> `insn` is the container
> `(set / call / compare)` is the real semantics

---

# 8. Mental model (very important)

Think of RTL as 3 layers:

### Layer 1: instruction graph

```text
insn → CFG + ordering
```

### Layer 2: operation tree

```text
(set ...)
(call ...)
(jump ...)
```

### Layer 3: expression tree

```text
(mem ...)
(reg ...)
(plus ...)
```

---

# 9. One-line intuition

If you want a compact mental translation:

> RTL is “assembly written as nested function calls over registers, memory, and constants”.

---

If you want next step, I can take one real `test.c.005t.gimple → RTL → assembly` chain and walk it end-to-end so you see exactly how GCC lowers code through each stage.

