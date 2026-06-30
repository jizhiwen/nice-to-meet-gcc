#!/usr/bin/env python3
"""
tinycc: tiny educational compiler frontend

Input: very small C-like / tiny-C++-style source
Output: x86_64 AT&T assembly
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


KEYWORDS = {"int", "return", "if", "else", "while"}
TOKEN_RE = re.compile(
    r"""
    (?P<ws>\s+)
  | (?P<comment>//[^\n]*)
  | (?P<number>\d+)
  | (?P<string>"([^"\\]|\\.)*")
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<op>==|!=|<=|>=|&&|\|\||[{}(),;=+\-*/%<>])
""",
    re.VERBOSE,
)


@dataclass
class Token:
    kind: str
    text: str
    pos: int


def tokenize(src: str) -> list[Token]:
    tokens: list[Token] = []
    idx = 0
    while idx < len(src):
        m = TOKEN_RE.match(src, idx)
        if not m:
            raise SyntaxError(f"Unexpected token near: {src[idx:idx + 20]!r}")
        idx = m.end()
        kind = m.lastgroup
        text = m.group()
        if kind in {"ws", "comment"}:
            continue
        if kind == "ident" and text in KEYWORDS:
            kind = text
        tokens.append(Token(kind, text, m.start()))
    tokens.append(Token("eof", "", len(src)))
    return tokens


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.toks = tokens
        self.i = 0

    def cur(self) -> Token:
        return self.toks[self.i]

    def peek(self, kind: str, text: str | None = None) -> bool:
        tok = self.cur()
        if tok.kind != kind:
            return False
        return text is None or tok.text == text

    def take(self, kind: str, text: str | None = None) -> Token:
        tok = self.cur()
        if tok.kind != kind or (text is not None and tok.text != text):
            expect = f"{kind} {text}" if text else kind
            got = f"{tok.kind} {tok.text!r}"
            raise SyntaxError(f"Expected {expect}, got {got} at {tok.pos}")
        self.i += 1
        return tok

    def maybe(self, kind: str, text: str | None = None) -> bool:
        if self.peek(kind, text):
            self.i += 1
            return True
        return False

    def parse(self) -> list[dict]:
        fns = []
        while not self.peek("eof"):
            fns.append(self.parse_function())
        return fns

    def parse_function(self) -> dict:
        self.take("int")
        name = self.take("ident").text
        self.take("op", "(")
        params = []
        if not self.peek("op", ")"):
            while True:
                self.take("int")
                params.append(self.take("ident").text)
                if not self.maybe("op", ","):
                    break
        self.take("op", ")")
        body = self.parse_block()
        return {"kind": "fn", "name": name, "params": params, "body": body}

    def parse_block(self) -> dict:
        self.take("op", "{")
        stmts = []
        while not self.peek("op", "}"):
            stmts.append(self.parse_stmt())
        self.take("op", "}")
        return {"kind": "block", "stmts": stmts}

    def parse_stmt(self) -> dict:
        if self.peek("int"):
            self.take("int")
            name = self.take("ident").text
            init = None
            if self.maybe("op", "="):
                init = self.parse_expr()
            self.take("op", ";")
            return {"kind": "decl", "name": name, "init": init}
        if self.peek("return"):
            self.take("return")
            value = self.parse_expr()
            self.take("op", ";")
            return {"kind": "return", "value": value}
        if self.peek("if"):
            self.take("if")
            self.take("op", "(")
            cond = self.parse_expr()
            self.take("op", ")")
            then_stmt = self.parse_stmt()
            else_stmt = None
            if self.maybe("else"):
                else_stmt = self.parse_stmt()
            return {"kind": "if", "cond": cond, "then": then_stmt, "else": else_stmt}
        if self.peek("while"):
            self.take("while")
            self.take("op", "(")
            cond = self.parse_expr()
            self.take("op", ")")
            body = self.parse_stmt()
            return {"kind": "while", "cond": cond, "body": body}
        if self.peek("op", "{"):
            return self.parse_block()
        if self.peek("ident"):
            # assignment statement
            name_tok = self.take("ident")
            if self.maybe("op", "="):
                rhs = self.parse_expr()
                self.take("op", ";")
                return {"kind": "assign", "name": name_tok.text, "value": rhs}
            # fallback: expression statement from identifier-start
            self.i -= 1
        expr = self.parse_expr()
        self.take("op", ";")
        return {"kind": "expr", "value": expr}

    def parse_expr(self) -> dict:
        return self.parse_eq()

    def parse_eq(self) -> dict:
        node = self.parse_rel()
        while self.peek("op", "==") or self.peek("op", "!="):
            op = self.take("op").text
            rhs = self.parse_rel()
            node = {"kind": "bin", "op": op, "lhs": node, "rhs": rhs}
        return node

    def parse_rel(self) -> dict:
        node = self.parse_add()
        while self.peek("op", "<") or self.peek("op", ">") or self.peek("op", "<=") or self.peek("op", ">="):
            op = self.take("op").text
            rhs = self.parse_add()
            node = {"kind": "bin", "op": op, "lhs": node, "rhs": rhs}
        return node

    def parse_add(self) -> dict:
        node = self.parse_mul()
        while self.peek("op", "+") or self.peek("op", "-"):
            op = self.take("op").text
            rhs = self.parse_mul()
            node = {"kind": "bin", "op": op, "lhs": node, "rhs": rhs}
        return node

    def parse_mul(self) -> dict:
        node = self.parse_unary()
        while self.peek("op", "*") or self.peek("op", "/") or self.peek("op", "%"):
            op = self.take("op").text
            rhs = self.parse_unary()
            node = {"kind": "bin", "op": op, "lhs": node, "rhs": rhs}
        return node

    def parse_unary(self) -> dict:
        if self.maybe("op", "-"):
            return {"kind": "neg", "value": self.parse_unary()}
        return self.parse_primary()

    def parse_primary(self) -> dict:
        if self.peek("number"):
            return {"kind": "num", "value": int(self.take("number").text)}
        if self.peek("string"):
            raw = self.take("string").text
            val = bytes(raw[1:-1], "utf-8").decode("unicode_escape")
            return {"kind": "str", "value": val}
        if self.peek("ident"):
            name = self.take("ident").text
            if self.maybe("op", "("):
                args = []
                if not self.peek("op", ")"):
                    while True:
                        args.append(self.parse_expr())
                        if not self.maybe("op", ","):
                            break
                self.take("op", ")")
                return {"kind": "call", "name": name, "args": args}
            return {"kind": "var", "name": name}
        if self.maybe("op", "("):
            node = self.parse_expr()
            self.take("op", ")")
            return node
        tok = self.cur()
        raise SyntaxError(f"Unexpected token {tok.kind} {tok.text!r} at {tok.pos}")


class Codegen:
    ARG_REGS = ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"]

    def __init__(self) -> None:
        self.lines: list[str] = []
        self.strings: list[tuple[str, str]] = []
        self.label_id = 0

    def new_label(self, prefix: str) -> str:
        self.label_id += 1
        return f".L_{prefix}_{self.label_id}"

    def emit(self, line: str) -> None:
        self.lines.append(line)

    def collect_locals(self, node: dict, out: set[str]) -> None:
        k = node["kind"]
        if k == "decl":
            out.add(node["name"])
        elif k == "block":
            for s in node["stmts"]:
                self.collect_locals(s, out)
        elif k == "if":
            self.collect_locals(node["then"], out)
            if node["else"] is not None:
                self.collect_locals(node["else"], out)
        elif k == "while":
            self.collect_locals(node["body"], out)

    def compile_function(self, fn: dict) -> None:
        locals_set = set(fn["params"])
        self.collect_locals(fn["body"], locals_set)
        ordered = list(fn["params"]) + sorted(x for x in locals_set if x not in fn["params"])
        slots = {name: -(idx + 1) * 8 for idx, name in enumerate(ordered)}
        stack_bytes = ((len(ordered) * 8 + 15) // 16) * 16
        ret_label = self.new_label("ret")

        self.emit(".text")
        self.emit(f".globl {fn['name']}")
        self.emit(f"{fn['name']}:")
        self.emit("  pushq %rbp")
        self.emit("  movq %rsp, %rbp")
        if stack_bytes:
            self.emit(f"  subq ${stack_bytes}, %rsp")
        for idx, p in enumerate(fn["params"]):
            if idx >= len(self.ARG_REGS):
                raise SyntaxError("Only up to 6 integer args are supported")
            self.emit(f"  movq {self.ARG_REGS[idx]}, {slots[p]}(%rbp)")
        self.emit_stmt(fn["body"], slots, ret_label)
        self.emit(f"{ret_label}:")
        self.emit("  leave")
        self.emit("  ret")

    def emit_stmt(self, node: dict, slots: dict[str, int], ret_label: str) -> None:
        k = node["kind"]
        if k == "block":
            for s in node["stmts"]:
                self.emit_stmt(s, slots, ret_label)
            return
        if k == "decl":
            if node["init"] is not None:
                self.emit_expr(node["init"], slots)
                self.emit(f"  movq %rax, {slots[node['name']]}(%rbp)")
            return
        if k == "assign":
            if node["name"] not in slots:
                raise SyntaxError(f"Unknown variable {node['name']}")
            self.emit_expr(node["value"], slots)
            self.emit(f"  movq %rax, {slots[node['name']]}(%rbp)")
            return
        if k == "expr":
            self.emit_expr(node["value"], slots)
            return
        if k == "return":
            self.emit_expr(node["value"], slots)
            self.emit(f"  jmp {ret_label}")
            return
        if k == "if":
            else_label = self.new_label("else")
            end_label = self.new_label("ifend")
            self.emit_expr(node["cond"], slots)
            self.emit("  cmpq $0, %rax")
            self.emit(f"  je {else_label}")
            self.emit_stmt(node["then"], slots, ret_label)
            self.emit(f"  jmp {end_label}")
            self.emit(f"{else_label}:")
            if node["else"] is not None:
                self.emit_stmt(node["else"], slots, ret_label)
            self.emit(f"{end_label}:")
            return
        if k == "while":
            top = self.new_label("while_top")
            done = self.new_label("while_done")
            self.emit(f"{top}:")
            self.emit_expr(node["cond"], slots)
            self.emit("  cmpq $0, %rax")
            self.emit(f"  je {done}")
            self.emit_stmt(node["body"], slots, ret_label)
            self.emit(f"  jmp {top}")
            self.emit(f"{done}:")
            return
        raise SyntaxError(f"Unhandled stmt kind {k}")

    def emit_expr(self, node: dict, slots: dict[str, int]) -> None:
        k = node["kind"]
        if k == "num":
            self.emit(f"  movq ${node['value']}, %rax")
            return
        if k == "var":
            if node["name"] not in slots:
                raise SyntaxError(f"Unknown variable {node['name']}")
            self.emit(f"  movq {slots[node['name']]}(%rbp), %rax")
            return
        if k == "str":
            lbl = self.new_label("str")
            self.strings.append((lbl, node["value"]))
            self.emit(f"  leaq {lbl}(%rip), %rax")
            return
        if k == "neg":
            self.emit_expr(node["value"], slots)
            self.emit("  negq %rax")
            return
        if k == "call":
            if len(node["args"]) > len(self.ARG_REGS):
                raise SyntaxError("Only up to 6 call args are supported")
            # Push args right-to-left so they are popped to ABI regs left-to-right.
            for arg in reversed(node["args"]):
                self.emit_expr(arg, slots)
                self.emit("  pushq %rax")
            for idx, _ in enumerate(node["args"]):
                self.emit(f"  popq {self.ARG_REGS[idx]}")
            self.emit(f"  call {node['name']}")
            return
        if k == "bin":
            op = node["op"]
            self.emit_expr(node["lhs"], slots)
            self.emit("  pushq %rax")
            self.emit_expr(node["rhs"], slots)
            self.emit("  popq %rcx")
            if op == "+":
                self.emit("  addq %rcx, %rax")
                return
            if op == "-":
                self.emit("  subq %rax, %rcx")
                self.emit("  movq %rcx, %rax")
                return
            if op == "*":
                self.emit("  imulq %rcx, %rax")
                return
            if op == "/":
                self.emit("  movq %rcx, %rdi")
                self.emit("  movq %rax, %rsi")
                self.emit("  call __tiny_div64")
                return
            if op == "%":
                self.emit("  movq %rcx, %rdi")
                self.emit("  movq %rax, %rsi")
                self.emit("  call __tiny_mod64")
                return
            if op in {"==", "!=", "<", "<=", ">", ">="}:
                self.emit("  cmpq %rax, %rcx")
                if op == "==":
                    self.emit("  sete %al")
                elif op == "!=":
                    self.emit("  setne %al")
                elif op == "<":
                    self.emit("  setl %al")
                elif op == "<=":
                    self.emit("  setle %al")
                elif op == ">":
                    self.emit("  setg %al")
                elif op == ">=":
                    self.emit("  setge %al")
                self.emit("  movzbq %al, %rax")
                return
            raise SyntaxError(f"Unsupported binary op {op}")
        raise SyntaxError(f"Unhandled expr kind {k}")

    def render(self) -> str:
        out = list(self.lines)
        if self.strings:
            out.append(".section .rodata")
            for lbl, s in self.strings:
                esc = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
                out.append(f"{lbl}:")
                out.append(f'  .asciz "{esc}"')
        out.append("")
        return "\n".join(out)


def compile_source(src: str) -> str:
    parser = Parser(tokenize(src))
    fns = parser.parse()
    codegen = Codegen()
    for fn in fns:
        codegen.compile_function(fn)
    return codegen.render()


def main() -> int:
    ap = argparse.ArgumentParser(description="tiny educational compiler")
    ap.add_argument("input", help="Input tiny source file")
    ap.add_argument("-o", "--output", required=True, help="Output assembly file")
    args = ap.parse_args()

    src_path = pathlib.Path(args.input)
    out_path = pathlib.Path(args.output)
    src = src_path.read_text(encoding="utf-8")
    asm = compile_source(src)
    out_path.write_text(asm, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"tinycc error: {exc}", file=sys.stderr)
        raise
