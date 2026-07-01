# boot_kernel — Minimal Linux + bash or tinysh via QEMU

Build an **x86_64 Linux kernel** and a tiny **initramfs** root filesystem in RAM. Two userland options:

| Shell | libc? | Size | Use case |
|-------|-------|------|----------|
| **bash** (default) | yes (glibc, static-linked) | ~1 MB | full interactive shell |
| **tinysh** | **no** | ~9 KB | minimal PID 1 + exec busybox applets |

Both variants include **busybox** (`mount`, `ls`, `cat`, …). All userland binaries are **built from source** — nothing copied from the host.

Layout mirrors `build-cross`:

```
boot_kernel/
├── config.sh
├── 01-download.sh
├── 02-build.sh
├── 03-run.sh
├── configs/boot.config          # kernel config fragment
├── rootfs/init                  # PID 1 (bash variant)
├── shell/crt0.S                 # _start for tinysh (-nostdlib)
├── shell/tiny_shell.c           # tiny shell (raw syscalls)
├── sources/                     # linux + bash + busybox (gitignored)
├── build/                       # out-of-tree builds (gitignored)
└── output/
    ├── bzImage
    ├── initramfs.cpio.gz        # bash variant
    └── initramfs-tiny.cpio.gz   # tinysh variant
```

---

## Requirements

- x86_64 Linux host (Ubuntu 22.04+)
- ~4 GB disk for kernel + bash sources and build
- Host **gcc/make** only (used to compile; not packaged into initramfs)

```bash
sudo apt install -y \
  build-essential curl cpio qemu-system-x86 libssl-dev
```

`libssl-dev` is only used to **build** the kernel host tool `extract-cert` (Linux 6.12 always compiles `certs/`). It is **not** copied into the initramfs.

---

## Quick start

**Bash variant (default):**

```bash
cd boot_kernel
chmod +x *.sh

./01-download.sh
./02-build.sh all
./03-run.sh
```

You should see a bash prompt:

```text
boot_kernel: Linux 6.12.5 — type 'exit' to halt
boot_kernel#
```

**Tinysh variant (no libc, no bash):**

```bash
./02-build.sh all-tiny
./03-run.sh tiny
```

Prompt:

```text
boot_kernel tinysh: no libc — busybox applets via execve
tinysh# ls /bin
```

Exit QEMU: **Ctrl-A**, then **X** (with `-nographic`).

The lines printed by `./03-run.sh` disappear once the kernel boot log starts — that is normal (same terminal). Launch parameters are saved to `output/last-qemu-run.txt`.

---

## Build steps

| Step | What | Output |
|------|------|--------|
| `kernel` | Linux `x86_64_defconfig` + `configs/boot.config` | `output/bzImage` |
| `bash` | GNU bash static (interactive shell) | `build/bash-install/bin/bash` |
| `tinysh` | Minimal shell via raw syscalls, `-nostdlib` | `build/tinysh/tinysh` |
| `busybox` | Static busybox — **`minimal`** (default) or `full` | `build/busybox/busybox` |
| `initramfs` | `/init` + bash + busybox applets | `output/initramfs.cpio.gz` |
| `initramfs-tiny` | tinysh + busybox (no bash) | `output/initramfs-tiny.cpio.gz` |

```bash
./02-build.sh list
./02-build.sh all-tiny          # kernel + tinysh + busybox mini
FORCE=1 ./02-build.sh initramfs
FORCE=1 ./02-build.sh initramfs-tiny
./02-build.sh clean
```

Logs: `logs/1-kernel.log` … `logs/4-initramfs.log`

---

## Root filesystem

There **is** a filesystem — it is the **initramfs** loaded into RAM at boot (`RAMDISK:` in dmesg). It is not on disk.

| Mount point | Type | Purpose |
|-------------|------|---------|
| `/` | initramfs (cpio) | `/bin`, `/init`, … |
| `/proc` | proc | process info |
| `/sys` | sysfs | kernel/devices |
| `/dev` | devtmpfs | device nodes |
| `/tmp` | tmpfs | temp files |

`/init` mounts these, then starts bash. Use `ls /`, `ls /bin`, `mount` to explore.

---

## Boot flow

**Bash variant:**

```text
QEMU → bzImage → initramfs → /init (PID 1) → exec /bin/bash
```

Kernel cmdline: `console=ttyS0 rdinit=/init panic=1`

**Tinysh variant:**

```text
QEMU → bzImage → initramfs → /bin/tinysh (PID 1) → exec busybox applets
```

Kernel cmdline: `console=ttyS0 rdinit=/bin/tinysh panic=1`

---

## Bash vs tinysh vs libc

| Component | Uses libc? | Notes |
|-----------|------------|-------|
| Linux kernel | no | runs in kernel mode |
| **bash** | yes | static-linked glibc embedded in the binary |
| **busybox** | musl/glibc internally | static-linked, but still a C library inside |
| **tinysh** | **no** | `-nostdlib`, only raw `syscall` instructions |

**Does bash use libc?** Yes. Even a "static" bash binary embeds glibc. The kernel does not use userspace libc; each userspace program brings its own (or, for tinysh, none).

**tinysh** is a ~9 KB program that:
- acts as PID 1 (mounts proc/sys/tmp/dev)
- reads lines from the serial console
- runs commands via `execve("/bin/<cmd>", …)` — busybox applets are symlinks under `/bin`
- has one built-in: `exit`

It is not a replacement for bash scripting — just enough to launch busybox commands without pulling in glibc.

---

## Versions

| Component | Version |
|-----------|---------|
| Linux | 6.12.5 |
| Bash | 5.2.21 |
| Busybox | 1.36.1 |

Change in `config.sh`, then re-download and rebuild.

---

## FAQ

### Kernel fails on `openssl/bio.h`

Linux 6.12 always builds the host tool `extract-cert` under `certs/` (see `Kbuild: obj-y += certs/`). Install OpenSSL **development headers on the build host**:

```bash
sudo apt install libssl-dev
rm -rf build/kernel build/.stamps/1-kernel.done
FORCE=1 ./02-build.sh kernel
```

This is a compile-time dependency only — nothing from OpenSSL goes into the initramfs.

### Busybox: mini vs full

Use **script steps** (default is **mini**):

```bash
./02-build.sh busybox          # mini (default)
./02-build.sh busybox-mini     # same as above
./02-build.sh busybox-full     # full defconfig
./02-build.sh all              # all steps, busybox mini
./02-build.sh all-full         # all steps, busybox full
```

Mini applets: `configs/busybox-minimal.applets` — `mount`, `ls`, `cat`, …

Edit that file to add/remove commands, then:

```bash
FORCE=1 ./02-build.sh busybox-mini initramfs
```

### No `ls` / `mount` after boot?

Bash alone is not a full userland. Rebuild with **busybox** (step 3) and repack initramfs:

```bash
./01-download.sh          # if busybox source missing
FORCE=1 ./02-build.sh busybox initramfs
./03-run.sh
```

### Static bash vs host copy

Initramfs contains only binaries built under `build/`. No host `/bin/*` or `.so` files are copied.

---

## References

- [Linux kernel build](https://www.kernel.org/doc/html/latest/)
- [GCC install guide](https://gcc.gnu.org/install/build.html)
