# aarch64-none-linux-gnu Cross-Toolchain — Build Guide

A step-by-step guide to building an **aarch64-none-linux-gnu** (arm64 Linux GNU) toolchain from source.

> **Naming note:** In GNU toolchains, arm64 is written as `aarch64`, not `arm64`.  
> A triplet has the form `arch-vendor-os-abi`, e.g. `aarch64-none-linux-gnu`.

---

## 1. Why a multi-stage build?

GCC and Glibc depend on each other:

- Building Glibc requires a **GCC that can emit target code**
- A full GCC needs **Glibc (and its headers) already installed**

Cross-compilation cannot use the native three-stage bootstrap, so we **bootstrap manually in stages**:

```
Linux headers → Binutils → GCC (minimal) → Glibc headers/startup → libgcc → Glibc → GCC (full)
```

---

## 2. Requirements

### Hardware

| Item | Recommendation |
|------|----------------|
| Disk | ≥ 20 GB free |
| RAM | ≥ 8 GB (GCC is memory-hungry) |
| CPU | Multi-core (`-j$(nproc)` parallel build) |

### Host system

- x86_64 Linux (this guide uses Ubuntu 22.04 as an example)
- **Native GCC ≥ 4.8** (GCC 9+ recommended)

### Build dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential bison flex gawk texinfo \
  libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
  curl xz-utils patch python3
```

---

## 3. Quick start (automated scripts)

`02-build.sh` runs the manual commands from Section 4 below, one step at a time, and writes logs. Each step is explained in the script header comments.

| Script step | What it does |
|-------------|--------------|
| `headers` | Install Linux kernel headers into sysroot |
| `binutils` | Build aarch64 `as`, `ld`, etc. |
| `gcc1` | Minimal cross C compiler (no libc yet) |
| `glibc-h` | Glibc headers, crt startup files, stub `libc.so` |
| `libgcc` | Finish libgcc (threads, etc.) |
| `glibc` | Full Glibc |
| `gcc2` | Full `gcc` / `g++` |
| `list` | Show per-step completion status |
| `clean` | Remove build directories and step stamps |

```bash
cd build-cross

# 1. Download and extract sources
chmod +x *.sh scripts/config.guess
./01-download.sh

# 2. Load environment variables
source config.sh

# 3. Full build (~1–3 hours)
./02-build.sh all

# 4. Verify
./03-verify.sh
```

### Custom install prefix

```bash
export PREFIX=/opt/cross-aarch64
export JOBS=16
source config.sh
./02-build.sh all
```

### Resume after interruption

```bash
source config.sh
./02-build.sh glibc    # Re-run step 6 only
./02-build.sh gcc2     # Re-run step 7 only
```

Logs are in `logs/`; names match step numbers (e.g. `6-glibc.log`).

### Inspect or force step rebuilds

`02-build.sh` stores completion stamps in `build/.stamps/`. If a step is already complete, it is skipped by default.

```bash
./02-build.sh list
FORCE=1 ./02-build.sh gcc2   # Rebuild even if already marked done
JOBS_GCC2=4 ./02-build.sh gcc2
./02-build.sh clean          # Remove build dirs and stamps
```

---

## 4. Manual step-by-step build

These commands match the scripts; use them to understand the process or debug by hand.

### 4.1 Set variables

```bash
export PREFIX=$PWD/cross-aarch64
export TARGET=aarch64-none-linux-gnu
export BUILD=$(gcc -dumpmachine)          # e.g. x86_64-linux-gnu
export HOST=$BUILD
export SYSROOT=$PREFIX/$TARGET/sysroot
export PATH=$PREFIX/bin:$PATH
export JOBS=$(nproc)

# Versions (keep in sync with config.sh)
export BINUTILS_VER=2.43.1
export GCC_VER=14.2.0
export GLIBC_VER=2.40
export LINUX_VER=6.12.5
```

### 4.2 Download sources

| Component | Purpose |
|-----------|---------|
| Linux kernel | Linux API headers under `usr/include` |
| Binutils | `as`, `ld`, `ar`, `ranlib`, etc. |
| GCC | Cross C/C++ compiler |
| Glibc | Target C library |
| GMP / MPFR / MPC / ISL | GCC deps (symlinked into GCC source tree) |

```bash
mkdir -p ~/toolchain-src && cd ~/toolchain-src

curl -LO https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz
curl -LO https://ftp.gnu.org/gnu/glibc/glibc-${GLIBC_VER}.tar.xz
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VER}.tar.xz
curl -LO https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz
curl -LO https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.1.tar.xz
curl -LO https://ftp.gnu.org/gnu/mpc/mpc-1.3.1.tar.gz
curl -LO https://libisl.sourceforge.io/isl-0.27.tar.xz

tar xf binutils-*.tar.xz
tar xf gcc-*.tar.xz
tar xf glibc-*.tar.xz
tar xf linux-*.tar.xz
tar xf gmp-*.tar.xz
tar xf mpfr-*.tar.xz
tar xf mpc-*.tar.gz
tar xf isl-*.tar.xz

# Symlink deps into the GCC source tree
cd gcc-${GCC_VER}
ln -sf ../gmp-6.3.0 gmp
ln -sf ../mpfr-4.2.1 mpfr
ln -sf ../mpc-1.3.1 mpc
ln -sf ../isl-0.27 isl
cd ..
```

### 4.3 Step 1 — Linux kernel headers

```bash
mkdir -p $SYSROOT/usr
cd ~/toolchain-src
make -C linux-${LINUX_VER} \
  ARCH=arm64 \
  INSTALL_HDR_PATH=$SYSROOT/usr \
  headers_install
```

Result: `$SYSROOT/usr/include/linux/`, `asm/`, etc.

### 4.4 Step 2 — Binutils

```bash
mkdir -p ~/toolchain-build/binutils && cd ~/toolchain-build/binutils

~/toolchain-src/binutils-${BINUTILS_VER}/configure \
  --prefix=$PREFIX \
  --target=$TARGET \
  --with-sysroot=$SYSROOT \
  --disable-multilib \
  --disable-nls \
  --disable-werror

make -j$JOBS
make install
```

Verify:

```bash
${TARGET}-as --version
${TARGET}-ld --version
```

### 4.5 Step 3 — GCC stage 1 (minimal C compiler)

```bash
mkdir -p ~/toolchain-build/gcc-stage1 && cd ~/toolchain-build/gcc-stage1

~/toolchain-src/gcc-${GCC_VER}/configure \
  --prefix=$PREFIX \
  --target=$TARGET \
  --build=$BUILD \
  --host=$HOST \
  --with-sysroot=$SYSROOT \
  --with-newlib \
  --without-headers \
  --enable-languages=c \
  --disable-shared \
  --disable-threads \
  --disable-nls \
  --disable-bootstrap \
  --disable-multilib \
  --with-system-zlib

make -j$JOBS all-gcc
make install-gcc
```

Verify:

```bash
${TARGET}-gcc --version
${TARGET}-gcc -dumpmachine   # should print aarch64-none-linux-gnu
```

### 4.6 Step 4 — Glibc headers and startup files

```bash
mkdir -p ~/toolchain-build/glibc-headers && cd ~/toolchain-build/glibc-headers

# No cross C++ yet — libc_cv_cxx_link_ok=no (CXX= alone is not enough; configure
# would still find host g++ and build links-dso-program with x86 code)
CC=${TARGET}-gcc libc_cv_cxx_link_ok=no \
~/toolchain-src/glibc-${GLIBC_VER}/configure \
  --prefix=/usr \
  --host=$TARGET \
  --build=$BUILD \
  --with-headers=$SYSROOT/usr/include \
  --disable-werror \
  --enable-kernel=4.19 \
  libc_cv_forced_unwind=yes \
  libc_cv_c_cleanup=yes

make install-bootstrap-headers=yes install-headers DESTDIR=$SYSROOT
make csu/subdir_lib
install csu/crt1.o csu/crti.o csu/crtn.o $SYSROOT/usr/lib/

# Stub libc.so for GCC stage 2 link tests
${TARGET}-gcc -nostdlib -nostartfiles -shared -x c /dev/null \
  -o $SYSROOT/usr/lib/libc.so
touch $SYSROOT/usr/include/gnu/stubs.h
```

### 4.7 Step 5 — Build libgcc

```bash
cd ~/toolchain-build/gcc-stage1
make -j$JOBS all-target-libgcc
make install-target-libgcc
```

### 4.8 Step 6 — Full Glibc

```bash
mkdir -p ~/toolchain-build/glibc && cd ~/toolchain-build/glibc

# Same libc_cv_cxx_link_ok=no as step 4
CC=${TARGET}-gcc libc_cv_cxx_link_ok=no \
~/toolchain-src/glibc-${GLIBC_VER}/configure \
  --prefix=/usr \
  --host=$TARGET \
  --build=$BUILD \
  --with-headers=$SYSROOT/usr/include \
  --disable-werror \
  --enable-kernel=4.19 \
  --with-default-link \
  libc_cv_forced_unwind=yes \
  libc_cv_c_cleanup=yes

make -j$JOBS
make DESTDIR=$SYSROOT install
```

### 4.9 Step 7 — GCC stage 2 (final toolchain)

```bash
mkdir -p ~/toolchain-build/gcc-stage2 && cd ~/toolchain-build/gcc-stage2

~/toolchain-src/gcc-${GCC_VER}/configure \
  --prefix=$PREFIX \
  --target=$TARGET \
  --build=$BUILD \
  --host=$HOST \
  --with-sysroot=$SYSROOT \
  --enable-languages=c,c++ \
  --disable-nls \
  --disable-bootstrap \
  --disable-multilib \
  --with-system-zlib

make -j$JOBS
make install
```

---

## 5. Install layout

```
$PREFIX/
├── bin/
│   ├── aarch64-none-linux-gnu-gcc
│   ├── aarch64-none-linux-gnu-g++
│   ├── aarch64-none-linux-gnu-ld
│   └── ...
└── aarch64-none-linux-gnu/
    └── sysroot/              ← target root filesystem
        ├── lib/
        │   └── ld-linux-aarch64.so.1
        └── usr/
            ├── include/      ← headers
            └── lib/          ← libc.so, libm.so, etc.
```

---

## 6. Usage

Add the toolchain to `PATH`:

```bash
export PATH=$PREFIX/bin:$PATH
```

### Compile examples

```bash
# Dynamic link (target must have matching Glibc)
${TARGET}-gcc -o hello hello.c

# Static link (runs on any aarch64 Linux)
${TARGET}-gcc -static -o hello_static hello.c

# C++
${TARGET}-g++ -o app app.cpp

# Explicit sysroot (recommended for cross userland builds)
${TARGET}-gcc --sysroot=$SYSROOT -o prog prog.c
```

### Check output architecture

```bash
file hello
# hello: ELF 64-bit LSB executable, ARM aarch64, ...
```

### Verify the toolchain state

```bash
./03-verify.sh
```

`03-verify.sh` now reports whether you are at:

- **stage 1** (C toolchain only; no `${TARGET}-g++` yet), or
- **full stage 2** (both C and C++ available)

If it reports stage 1, run:

```bash
./02-build.sh gcc2
./03-verify.sh
```

---

## 7. FAQ

### 1. `configure: error: Building GCC requires GMP 4.2+, MPFR 3.1.0+ and MPC 0.8.0+`

Ensure GMP/MPFR/MPC/ISL are symlinked under `gcc-${GCC_VER}/`.

### 2. Glibc build fails early in bootstrap

Re-check Step 3/4 ordering and configuration. In this flow, keep `gcc1` as C-only and keep `libc_cv_cxx_link_ok=no` in Glibc configure to avoid host C++ leakage.

### 3. `cannot find crt1.o` or `cannot find -lc`

Glibc headers/startup step did not finish, or `SYSROOT` paths disagree.  
Every `--with-sysroot` must point at the same `$SYSROOT`.

### 4. Difference from `aarch64-linux-gnu-gcc` (apt package)

Ubuntu’s `gcc-aarch64-linux-gnu` is prebuilt; this flow builds from source so you can pick versions, patches, and options.

### 5. `links-dso-program.o: file in wrong format` / `EM: 62`

**Symptom:** At Glibc step 6, `ld` reports `Relocations in generic ELF (EM: 62)` and the link line includes `-lstdc++` and `links-dso-program.o` (not `links-dso-program-c`).

**Cause:** GCC step 3 builds C only; `${TARGET}-g++` does not exist yet. Glibc configure runs `AC_PROG_CXX`, finds the host `g++`, and its link test succeeds on the build machine. It then builds a C++ helper with host `g++` (x86_64, EM: 62) and links it with the aarch64 `ld`.

**Fix:** Pass **`libc_cv_cxx_link_ok=no`** to Glibc configure (steps 4 and 6 in `02-build.sh`). That forces `CXX` to stay empty so Glibc uses the pure-C `links-dso-program-c`. **`CXX=` alone is not enough** — configure still discovers host `g++`.

After changing configure options, remove the old build dir and re-run:

```bash
rm -rf build/glibc
./02-build.sh glibc
```

### 6. Bare-metal / newlib only (no Glibc)

Use a different target (e.g. `aarch64-none-elf`); no Glibc, simpler flow, but you cannot build normal Linux userland programs.

---

## 8. Component versions

| Component | Version | Notes |
|-----------|---------|-------|
| Binutils | 2.43.1 | 2024 stable |
| GCC | 14.2.0 | 2024 stable |
| Glibc | 2.40 | Pairs with GCC 14 |
| Linux | 6.12.5 | Kernel API headers |

Change versions in `config.sh`, then re-download and rebuild.

---

## 9. References

- [GCC — Building a cross-compiler](https://gcc.gnu.org/install/building.html)
- [Linux From Scratch — AArch64 toolchain](https://www.linuxfromscratch.org/lfs/view/stable/partintro/toolchaintechnotes.html)
- [Preshing — How to Build a GCC Cross-Compiler](https://preshing.com/20141119/how-to-build-a-gcc-cross-compiler/)
