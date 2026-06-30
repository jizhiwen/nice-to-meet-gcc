#!/usr/bin/env python3
"""
tinyld: tiny linker implementation

Reads tinyobj-v1 files and writes a Linux x86_64 ELF executable directly.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys

BASE_VADDR = 0x400000
LOAD_OFFSET = 0x1000


def align_up(value: int, align: int) -> int:
    return (value + align - 1) & ~(align - 1)


def read_obj(path: str) -> dict:
    obj = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    if obj.get("format") != "tinyobj-v1":
        raise ValueError(f"{path}: unsupported object format")
    return obj


def write_elf64_exec(output: pathlib.Path, entry: int, text: bytes, rodata: bytes, data: bytes, bss_size: int) -> None:
    text_off = LOAD_OFFSET
    rodata_off = align_up(text_off + len(text), 16)
    data_off = align_up(rodata_off + len(rodata), 16)
    file_end = data_off + len(data)

    text_va = BASE_VADDR + text_off
    rodata_va = BASE_VADDR + rodata_off
    data_va = BASE_VADDR + data_off
    mem_end = align_up(data_va + len(data) + bss_size, 16)

    # ELF header
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0, 0]) + bytes(7)
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        e_ident,
        2,      # ET_EXEC
        62,     # EM_X86_64
        1,      # EV_CURRENT
        entry,
        64,     # e_phoff
        0,      # e_shoff
        0,      # e_flags
        64,     # e_ehsize
        56,     # e_phentsize
        1,      # e_phnum
        0,
        0,
        0,
    )

    # Program header: one RWX load segment
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,          # PT_LOAD
        7,          # PF_R|PF_W|PF_X
        0,          # p_offset
        BASE_VADDR, # p_vaddr
        BASE_VADDR, # p_paddr
        file_end,
        mem_end - BASE_VADDR,
        0x1000,
    )

    image = bytearray()
    image.extend(ehdr)
    image.extend(phdr)
    if len(image) > LOAD_OFFSET:
        raise ValueError("headers exceed load offset")
    image.extend(b"\x00" * (LOAD_OFFSET - len(image)))

    image.extend(text)
    image.extend(b"\x00" * (rodata_off - len(image)))
    image.extend(rodata)
    image.extend(b"\x00" * (data_off - len(image)))
    image.extend(data)

    output.write_bytes(image)
    output.chmod(0o755)


def main() -> int:
    ap = argparse.ArgumentParser(description="tiny linker")
    ap.add_argument("-o", "--output", required=True, help="Output executable")
    ap.add_argument("--entry", default="_start", help="Entry symbol")
    ap.add_argument("--map", default="", help="Optional map file")
    ap.add_argument("objects", nargs="+", help="Input tiny object files")
    args = ap.parse_args()

    objs = [(path, read_obj(path)) for path in args.objects]

    text = bytearray()
    rodata = bytearray()
    data = bytearray()
    bss_size = 0

    bases: dict[tuple[str, str], int] = {}
    sym_addr: dict[str, int] = {}

    # Layout sections and remember per-object section base offsets.
    for path, obj in objs:
        t = bytes.fromhex(obj["sections"]["text"])
        r = bytes.fromhex(obj["sections"]["rodata"])
        d = bytes.fromhex(obj["sections"]["data"])
        b = int(obj["sections"]["bss_size"])

        bases[(path, "text")] = len(text)
        text.extend(t)

        rodata_base = align_up(len(rodata), 16)
        if rodata_base > len(rodata):
            rodata.extend(b"\x00" * (rodata_base - len(rodata)))
        bases[(path, "rodata")] = rodata_base
        rodata.extend(r)

        data_base = align_up(len(data), 16)
        if data_base > len(data):
            data.extend(b"\x00" * (data_base - len(data)))
        bases[(path, "data")] = data_base
        data.extend(d)

        bases[(path, "bss")] = bss_size
        bss_size = align_up(bss_size + b, 16)

    text_va = BASE_VADDR + LOAD_OFFSET
    rodata_va = text_va + align_up(len(text), 16)
    data_va = rodata_va + align_up(len(rodata), 16)
    bss_va = data_va + align_up(len(data), 16)

    def sec_addr(sec: str, off: int) -> int:
        if sec == "text":
            return text_va + off
        if sec == "rodata":
            return rodata_va + off
        if sec == "data":
            return data_va + off
        if sec == "bss":
            return bss_va + off
        raise ValueError(f"unknown section {sec}")

    # Build symbol table.
    for path, obj in objs:
        for s in obj["symbols"]:
            if s["section"] == "undef":
                continue
            sec = s["section"]
            off = int(s["offset"]) + bases[(path, sec)]
            addr = sec_addr(sec, off)
            name = s["name"]
            if s.get("global", False):
                if name in sym_addr:
                    raise ValueError(f"duplicate global symbol: {name}")
                sym_addr[name] = addr
            else:
                sym_addr[f"{path}::{name}"] = addr

    # Resolve undefined globals used by relocations.
    for path, obj in objs:
        for s in obj["symbols"]:
            if s["section"] == "undef" and s.get("global", False):
                if s["name"] not in sym_addr:
                    raise ValueError(f"undefined symbol: {s['name']}")

    # Prepare combined mutable image for reloc patching.
    text_buf = bytearray(text)
    rodata_buf = bytearray(rodata)
    data_buf = bytearray(data)

    def patch32(buf: bytearray, off: int, val: int) -> None:
        if not (-2**31 <= val < 2**31):
            raise ValueError(f"rel32 out of range: {val}")
        buf[off:off + 4] = struct.pack("<i", val)

    def patch64(buf: bytearray, off: int, val: int) -> None:
        buf[off:off + 8] = struct.pack("<Q", val & 0xFFFFFFFFFFFFFFFF)

    for path, obj in objs:
        for r in obj["relocations"]:
            sec = r["section"]
            local_off = int(r["offset"])
            glob_off = local_off + bases[(path, sec)]
            sym = r["symbol"]
            addend = int(r.get("addend", 0))

            if sym in sym_addr:
                saddr = sym_addr[sym]
            else:
                local_key = f"{path}::{sym}"
                if local_key not in sym_addr:
                    raise ValueError(f"undefined relocation symbol: {sym}")
                saddr = sym_addr[local_key]

            if sec == "text":
                buf = text_buf
                paddr_base = text_va
            elif sec == "rodata":
                buf = rodata_buf
                paddr_base = rodata_va
            elif sec == "data":
                buf = data_buf
                paddr_base = data_va
            else:
                raise ValueError(f"unsupported relocation section: {sec}")

            if r["type"] == "rel32":
                p = paddr_base + glob_off
                val = saddr + addend - (p + 4)
                patch32(buf, glob_off, val)
            elif r["type"] == "abs64":
                patch64(buf, glob_off, saddr + addend)
            else:
                raise ValueError(f"unsupported relocation type: {r['type']}")

    if args.entry not in sym_addr:
        raise ValueError(f"entry symbol not found: {args.entry}")

    output = pathlib.Path(args.output)
    write_elf64_exec(output, sym_addr[args.entry], bytes(text_buf), bytes(rodata_buf), bytes(data_buf), bss_size)

    if args.map:
        map_path = pathlib.Path(args.map)
        lines = ["# tinyld map"]
        for name in sorted(sym_addr):
            if "::" in name:
                continue
            lines.append(f"{name} 0x{sym_addr[name]:016x}")
        map_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"tinyld error: {exc}", file=sys.stderr)
        raise
