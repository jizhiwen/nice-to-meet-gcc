#!/usr/bin/env python3
"""
tinyas: tiny assembler implementation

Produces a tiny JSON object format consumed by tinyld.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
import sys
from dataclasses import dataclass

SECTIONS = {".text": "text", ".rodata": "rodata", ".data": "data", ".bss": "bss"}
REGS = {
    "al": 0,
    "rax": 0,
    "rcx": 1,
    "rdx": 2,
    "rbx": 3,
    "rsp": 4,
    "rbp": 5,
    "rsi": 6,
    "rdi": 7,
    "r8": 8,
    "r9": 9,
}


@dataclass
class Reloc:
    section: str
    offset: int
    rtype: str
    symbol: str
    addend: int


class Asm:
    def __init__(self, source: pathlib.Path) -> None:
        self.source = source
        self.section = "text"
        self.buf = {k: bytearray() for k in ("text", "rodata", "data")}
        self.bss = 0
        self.globals: set[str] = set()
        self.symbols: dict[str, tuple[str, int]] = {}
        self.relocs: list[Reloc] = []

    def pos(self) -> int:
        if self.section == "bss":
            return self.bss
        return len(self.buf[self.section])

    def emit(self, data: bytes) -> None:
        if self.section == "bss":
            self.bss += len(data)
        else:
            self.buf[self.section].extend(data)

    def emit_u32(self, value: int) -> None:
        self.emit(struct.pack("<I", value & 0xFFFFFFFF))

    def rex(self, w: int, r: int, x: int, b: int) -> bytes:
        if not (w or r or x or b):
            return b""
        return bytes([0x40 | (w << 3) | (r << 2) | (x << 1) | b])

    def modrm(self, mod: int, reg: int, rm: int) -> bytes:
        return bytes([((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7)])

    def parse_reg(self, op: str) -> int:
        if not op.startswith("%"):
            raise ValueError(f"expected register, got {op}")
        name = op[1:]
        if name not in REGS:
            raise ValueError(f"unsupported register {op}")
        return REGS[name]

    def parse_imm(self, op: str) -> int:
        if not op.startswith("$"):
            raise ValueError(f"expected immediate, got {op}")
        return int(op[1:], 0)

    def parse_mem(self, op: str) -> tuple[int, int]:
        m = re.fullmatch(r"(-?\d+)?\((%[a-z0-9]+)\)", op)
        if not m:
            raise ValueError(f"bad memory operand {op}")
        disp = int(m.group(1) or "0")
        base = self.parse_reg(m.group(2))
        return base, disp

    def parse_rip_label(self, op: str) -> str | None:
        m = re.fullmatch(r"([A-Za-z_.$][A-Za-z0-9_.$]*)\(%rip\)", op)
        return m.group(1) if m else None

    def emit_reg_mem(self, opcode: int, src_reg: int, dst_base: int, dst_disp: int) -> None:
        rex = self.rex(1, 1 if src_reg >= 8 else 0, 0, 1 if dst_base >= 8 else 0)
        self.emit(rex + bytes([opcode]))
        if dst_base == 4:
            if -128 <= dst_disp <= 127:
                self.emit(self.modrm(1, src_reg, 4) + bytes([0x24, dst_disp & 0xFF]))
            else:
                self.emit(self.modrm(2, src_reg, 4) + bytes([0x24]) + struct.pack("<i", dst_disp))
            return
        if dst_disp == 0 and dst_base != 5:
            self.emit(self.modrm(0, src_reg, dst_base))
        elif -128 <= dst_disp <= 127:
            self.emit(self.modrm(1, src_reg, dst_base) + bytes([dst_disp & 0xFF]))
        else:
            self.emit(self.modrm(2, src_reg, dst_base) + struct.pack("<i", dst_disp))

    def emit_mem_reg(self, opcode: int, src_base: int, src_disp: int, dst_reg: int) -> None:
        rex = self.rex(1, 1 if dst_reg >= 8 else 0, 0, 1 if src_base >= 8 else 0)
        self.emit(rex + bytes([opcode]))
        if src_base == 4:
            if -128 <= src_disp <= 127:
                self.emit(self.modrm(1, dst_reg, 4) + bytes([0x24, src_disp & 0xFF]))
            else:
                self.emit(self.modrm(2, dst_reg, 4) + bytes([0x24]) + struct.pack("<i", src_disp))
            return
        if src_disp == 0 and src_base != 5:
            self.emit(self.modrm(0, dst_reg, src_base))
        elif -128 <= src_disp <= 127:
            self.emit(self.modrm(1, dst_reg, src_base) + bytes([src_disp & 0xFF]))
        else:
            self.emit(self.modrm(2, dst_reg, src_base) + struct.pack("<i", src_disp))

    def emit_rel32(self, opbytes: bytes, symbol: str) -> None:
        self.emit(opbytes)
        off = self.pos()
        self.emit_u32(0)
        self.relocs.append(Reloc(self.section, off, "rel32", symbol, 0))

    def assemble_line(self, line: str, lineno: int) -> None:
        line = line.split("#", 1)[0].strip()
        if not line:
            return
        if line.endswith(":"):
            name = line[:-1]
            self.symbols[name] = (self.section, self.pos())
            return
        if line.startswith("."):
            parts = line.split(None, 1)
            d = parts[0]
            rest = parts[1] if len(parts) > 1 else ""
            if d in (".text", ".data", ".bss", ".rodata"):
                self.section = SECTIONS[d]
                return
            if d == ".section":
                sec = rest.strip()
                if sec not in SECTIONS:
                    raise ValueError(f"{self.source}:{lineno}: unsupported section {sec}")
                self.section = SECTIONS[sec]
                return
            if d == ".globl":
                self.globals.add(rest.strip())
                return
            if d == ".asciz":
                s = rest.strip()
                if not (s.startswith('"') and s.endswith('"')):
                    raise ValueError(f"{self.source}:{lineno}: bad .asciz")
                val = bytes(s[1:-1], "utf-8").decode("unicode_escape").encode("utf-8") + b"\x00"
                self.emit(val)
                return
            if d == ".byte":
                self.emit(bytes([int(rest.strip(), 0) & 0xFF]))
                return
            if d == ".long":
                self.emit(struct.pack("<I", int(rest.strip(), 0) & 0xFFFFFFFF))
                return
            if d == ".quad":
                arg = rest.strip()
                if re.fullmatch(r"-?\d+", arg):
                    self.emit(struct.pack("<Q", int(arg, 0) & 0xFFFFFFFFFFFFFFFF))
                else:
                    off = self.pos()
                    self.emit(struct.pack("<Q", 0))
                    self.relocs.append(Reloc(self.section, off, "abs64", arg, 0))
                return
            if d in (".type", ".size"):
                return
            raise ValueError(f"{self.source}:{lineno}: unsupported directive {d}")

        m = re.match(r"([a-z]+)\s*(.*)$", line)
        if not m:
            raise ValueError(f"{self.source}:{lineno}: bad instruction")
        op = m.group(1)
        operands = [x.strip() for x in m.group(2).split(",")] if m.group(2) else []

        if op == "pushq":
            r = self.parse_reg(operands[0])
            self.emit(self.rex(0, 0, 0, 1 if r >= 8 else 0) + bytes([0x50 + (r & 7)]))
            return
        if op == "popq":
            r = self.parse_reg(operands[0])
            self.emit(self.rex(0, 0, 0, 1 if r >= 8 else 0) + bytes([0x58 + (r & 7)]))
            return
        if op == "leave":
            self.emit(b"\xC9")
            return
        if op == "ret":
            self.emit(b"\xC3")
            return
        if op == "syscall":
            self.emit(b"\x0F\x05")
            return
        if op == "hlt":
            self.emit(b"\xF4")
            return
        if op == "cqo":
            self.emit(b"\x48\x99")
            return

        if op in {"jmp", "je", "jne", "call"}:
            sym = operands[0]
            if op == "jmp":
                self.emit_rel32(b"\xE9", sym)
            elif op == "je":
                self.emit_rel32(b"\x0F\x84", sym)
            elif op == "jne":
                self.emit_rel32(b"\x0F\x85", sym)
            else:
                self.emit_rel32(b"\xE8", sym)
            return

        if op in {"sete", "setne", "setl", "setle", "setg", "setge"}:
            r = self.parse_reg(operands[0])
            if r != 0:
                raise ValueError(f"{self.source}:{lineno}: only %al supported for {op}")
            cc = {
                "sete": 0x94,
                "setne": 0x95,
                "setl": 0x9C,
                "setle": 0x9E,
                "setg": 0x9F,
                "setge": 0x9D,
            }[op]
            self.emit(bytes([0x0F, cc, 0xC0]))
            return

        if op == "idivq":
            r = self.parse_reg(operands[0])
            self.emit(self.rex(1, 0, 0, 1 if r >= 8 else 0) + b"\xF7" + self.modrm(3, 7, r))
            return

        if op == "movzbq":
            src = self.parse_reg(operands[0])
            dst = self.parse_reg(operands[1])
            if src != 0:
                raise ValueError(f"{self.source}:{lineno}: only %al source supported")
            self.emit(self.rex(1, 1 if dst >= 8 else 0, 0, 0) + b"\x0F\xB6" + self.modrm(3, dst, 0))
            return

        if op == "negq":
            r = self.parse_reg(operands[0])
            self.emit(self.rex(1, 0, 0, 1 if r >= 8 else 0) + b"\xF7" + self.modrm(3, 3, r))
            return

        if op == "movq":
            src, dst = operands
            if src.startswith("$") and dst.startswith("%"):
                imm = self.parse_imm(src)
                r = self.parse_reg(dst)
                self.emit(self.rex(1, 0, 0, 1 if r >= 8 else 0) + b"\xC7" + self.modrm(3, 0, r) + struct.pack("<i", imm))
                return
            if src.startswith("%") and dst.startswith("%"):
                s = self.parse_reg(src)
                d = self.parse_reg(dst)
                self.emit(self.rex(1, 1 if s >= 8 else 0, 0, 1 if d >= 8 else 0) + b"\x89" + self.modrm(3, s, d))
                return
            if src.startswith("%") and "(" in dst:
                s = self.parse_reg(src)
                base, disp = self.parse_mem(dst)
                self.emit_reg_mem(0x89, s, base, disp)
                return
            if "(" in src and dst.startswith("%"):
                base, disp = self.parse_mem(src)
                d = self.parse_reg(dst)
                self.emit_mem_reg(0x8B, base, disp, d)
                return
            raise ValueError(f"{self.source}:{lineno}: unsupported movq operands {src}, {dst}")

        if op == "leaq":
            src, dst = operands
            d = self.parse_reg(dst)
            label = self.parse_rip_label(src)
            if label is not None:
                self.emit(self.rex(1, 1 if d >= 8 else 0, 0, 0) + b"\x8D" + self.modrm(0, d, 5))
                off = self.pos()
                self.emit_u32(0)
                self.relocs.append(Reloc(self.section, off, "rel32", label, 0))
                return
            base, disp = self.parse_mem(src)
            self.emit_mem_reg(0x8D, base, disp, d)
            return

        if op in {"addq", "subq", "xorq", "cmpq"}:
            src, dst = operands
            if src.startswith("$") and dst.startswith("%") and op in {"subq", "cmpq"}:
                imm = self.parse_imm(src)
                d = self.parse_reg(dst)
                ext = 5 if op == "subq" else 7
                self.emit(self.rex(1, 0, 0, 1 if d >= 8 else 0) + b"\x81" + self.modrm(3, ext, d) + struct.pack("<i", imm))
                return
            s = self.parse_reg(src)
            d = self.parse_reg(dst)
            opc = {"addq": 0x01, "subq": 0x29, "xorq": 0x31, "cmpq": 0x39}[op]
            self.emit(self.rex(1, 1 if s >= 8 else 0, 0, 1 if d >= 8 else 0) + bytes([opc]) + self.modrm(3, s, d))
            return

        if op == "imulq":
            s = self.parse_reg(operands[0])
            d = self.parse_reg(operands[1])
            self.emit(self.rex(1, 1 if d >= 8 else 0, 0, 1 if s >= 8 else 0) + b"\x0F\xAF" + self.modrm(3, d, s))
            return

        raise ValueError(f"{self.source}:{lineno}: unsupported opcode {op}")

    def assemble(self) -> dict:
        lines = self.source.read_text(encoding="utf-8").splitlines()
        for lineno, line in enumerate(lines, start=1):
            self.assemble_line(line, lineno)
        symbols = []
        for name in sorted(self.symbols):
            sec, off = self.symbols[name]
            symbols.append({
                "name": name,
                "section": sec,
                "offset": off,
                "global": name in self.globals,
            })
        for g in sorted(self.globals):
            if g not in self.symbols:
                symbols.append({"name": g, "section": "undef", "offset": 0, "global": True})
        return {
            "format": "tinyobj-v1",
            "sections": {
                "text": self.buf["text"].hex(),
                "rodata": self.buf["rodata"].hex(),
                "data": self.buf["data"].hex(),
                "bss_size": self.bss,
            },
            "symbols": symbols,
            "relocations": [
                {
                    "section": r.section,
                    "offset": r.offset,
                    "type": r.rtype,
                    "symbol": r.symbol,
                    "addend": r.addend,
                }
                for r in self.relocs
            ],
        }


def main() -> int:
    ap = argparse.ArgumentParser(description="tiny assembler")
    ap.add_argument("input", help="Input assembly")
    ap.add_argument("-o", "--output", required=True, help="Output tiny object")
    args = ap.parse_args()

    src = pathlib.Path(args.input)
    out = pathlib.Path(args.output)
    obj = Asm(src).assemble()
    out.write_text(json.dumps(obj, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"tinyas error: {exc}", file=sys.stderr)
        raise
