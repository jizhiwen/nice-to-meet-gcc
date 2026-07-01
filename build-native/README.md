# Native Linux Toolchain — Build Guide

Build a **native** Linux toolchain (host == target) from source, using the **same component versions** as `build-cross`.

---

## 1. Overview

| | build-cross | build-native |
|---|-------------|--------------|
| Target | `aarch64-none-linux-gnu` | `$BUILD` (e.g. `x86_64-linux-gnu`) |
| Install prefix | `cross-aarch64/` | `native-x86_64/` |
| Tool names | `aarch64-none-linux-gnu-gcc` | `gcc`, `g++` |
| Sources | `build-cross/sources/` | `build-native/sources/` |

Bootstrap order (same as cross):

```
Linux headers → Binutils → GCC (minimal) → Glibc headers/startup → libgcc → Glibc → GCC (full)
```

---

## 2. Requirements

Same as `build-cross`:

- x86_64 Linux (Ubuntu 22.04 recommended)
- ≥ 20 GB disk, ≥ 8 GB RAM
- Native GCC ≥ 4.8 (GCC 9+ recommended)

```bash
sudo apt install -y \
  build-essential bison flex gawk texinfo \
  libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
  curl xz-utils patch python3
```

---

## 3. Quick start

```bash
cd build-native

# 1. Download and extract sources
chmod +x *.sh scripts/config.guess
./01-download.sh

# 2. Load environment
source config.sh

# 3. Full build (~1–3 hours)
./02-build.sh all

# 4. Verify
./03-verify.sh
```

### Custom install prefix

```bash
export PREFIX=/opt/native-x86_64
export JOBS=16
source config.sh
./02-build.sh all
```

### Resume after interruption

```bash
source config.sh
./02-build.sh glibc
./02-build.sh gcc2
```

Logs are in `logs/` (e.g. `6-glibc.log`).

---

## 4. Usage

```bash
export PATH="$PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$PREFIX/usr/lib:$PREFIX/lib:$LD_LIBRARY_PATH"

gcc --sysroot="$PREFIX" -o hello hello.c
./hello

g++ --sysroot="$PREFIX" -o app app.cpp
```

New Glibc lives under `$PREFIX/usr/lib`. The system dynamic linker does not know that path, so set `LD_LIBRARY_PATH` (or use `-Wl,-rpath,$PREFIX/usr/lib` when linking).

---

## 5. Component versions

| Component | Version |
|-----------|---------|
| Binutils | 2.43.1 |
| GCC | 14.2.0 |
| Glibc | 2.40 |
| Linux headers | 6.12.5 |

Change versions in `config.sh` (keep in sync with `build-cross/config.sh`).

---

## 6. Differences from build-cross

- No `--target` on Binutils/GCC (native build)
- `CC=$PREFIX/bin/gcc` instead of `${TARGET}-gcc` for Glibc
- `SYSROOT=$PREFIX` (no `$TARGET/sysroot` subtree)
- Linux headers use `ARCH=x86_64` (auto-selected from `$BUILD`)
- `libc_cv_cxx_link_ok=no` still required in Glibc steps 4 and 6 (same host C++ leakage issue as cross)

See `build-cross/README.md` FAQ #5 for details on `libc_cv_cxx_link_ok`.

### Binutils: `relocation R_X86_64_32 ... can not be used when making a PIE object` (gprofng)

Re-running step 2 while `$PREFIX/bin` is already on `PATH` (from a previous build) makes binutils link gprofng with the bootstrap `ld`, which defaults to PIE. `02-build.sh` builds binutils with **system** `ld` (PREFIX stripped from PATH). If you configured by hand, unset PATH to PREFIX first, or add `--enable-gprofng=no`.

```bash
FORCE=1 ./02-build.sh binutils
```

### GCC stage 1 / 2: host tool failures (gengtype, isl, gmp segfault)

`gcc1` and `gcc2` build host-side tools with the **system** compiler/linker (`host_path`), not `$PREFIX/bin` from a partial bootstrap install. Target `as`/`ld` come from step 2 via `--with-as` / `--with-ld`. Step `gcc1` also runs `clean_partial_gcc` to remove a broken partial `$PREFIX/bin/gcc`.

Do **not** link host tools to the new glibc in PREFIX during `gcc2` — on hosts with older system glibc, running `build/gengtype` fails with `GLIBC_2.38 not found`.

```bash
FORCE=1 ./02-build.sh gcc1
# or
FORCE=1 ./02-build.sh gcc2
```

---

## 7. References

- `../build-cross/README.md` — cross toolchain guide (same versions, more detail)
- [GCC — Building](https://gcc.gnu.org/install/building.html)
- [Linux From Scratch](https://www.linuxfromscratch.org/lfs/view/stable/)
