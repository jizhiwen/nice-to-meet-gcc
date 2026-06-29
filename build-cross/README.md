# aarch64-none-linux-gnu 交叉编译工具链 — 完整构建指南

从零构建 **aarch64-none-linux-gnu**（即 arm64 Linux GNU 工具链）的完整流程。

> **命名说明**：GNU 工具链中 arm64 的标准写法是 `aarch64`，不是 `arm64`。  
> 三元组格式为 `架构-厂商-操作系统-ABI`，例如 `aarch64-none-linux-gnu`。

---

## 一、为什么需要多阶段构建？

GCC 与 Glibc 存在循环依赖：

- 编译 Glibc 需要 **已能生成目标代码的 GCC**
- 完整功能的 GCC 又需要 **已安装的目标 C 库（Glibc）和头文件**

交叉编译无法做 native 的三阶段 bootstrap，因此采用 **分阶段手动引导**：

```
Linux 头文件 → Binutils → GCC(最小) → Glibc 头/启动文件 → libgcc → Glibc → GCC(完整)
```

---

## 二、环境要求

### 硬件

| 项目 | 建议 |
|------|------|
| 磁盘 | ≥ 20 GB 可用空间 |
| 内存 | ≥ 8 GB（GCC 编译较耗内存） |
| CPU | 多核（`-j$(nproc)` 并行编译） |

### 宿主系统

- x86_64 Linux（本指南以 Ubuntu 22.04 为例）
- **原生 GCC ≥ 4.8**（建议 GCC 9+）

### 安装构建依赖

```bash
sudo apt update
sudo apt install -y \
  build-essential bison flex gawk texinfo \
  libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
  curl xz-utils patch python3
```

---

## 三、快速开始（自动化脚本）

本仓库提供一键脚本，推荐首次使用：

```bash
cd /home/zw/build-cross

# 1. 下载并解压全部源码
chmod +x *.sh scripts/config.guess
./01-download.sh

# 2. 加载环境变量
source config.sh

# 3. 完整构建（约 1~3 小时，视机器而定）
./02-build.sh all

# 4. 验证
./03-verify.sh
```

### 自定义安装路径

```bash
export PREFIX=/opt/cross-aarch64
export JOBS=16
source config.sh
./02-build.sh all
```

### 从某步恢复（构建中断时）

```bash
source config.sh
./02-build.sh glibc    # 只重跑 Glibc
./02-build.sh gcc2     # 只重跑最终 GCC
```

日志保存在 `logs/` 目录。

---

## 四、手动逐步构建（完整命令）

以下命令与脚本逻辑一致，便于理解原理或手动调试。

### 4.1 设置变量

```bash
export PREFIX=$HOME/cross-aarch64
export TARGET=aarch64-none-linux-gnu
export BUILD=$(gcc -dumpmachine)          # 例如 x86_64-linux-gnu
export HOST=$BUILD
export SYSROOT=$PREFIX/$TARGET/sysroot
export PATH=$PREFIX/bin:$PATH
export JOBS=$(nproc)

# 版本号（与 config.sh 保持一致）
export BINUTILS_VER=2.43.1
export GCC_VER=14.2.0
export GLIBC_VER=2.40
export LINUX_VER=6.12.5
```

### 4.2 下载源码

| 组件 | 用途 |
|------|------|
| Linux 内核 | 提供 `usr/include` 下的 Linux API 头文件 |
| Binutils | `as`、`ld`、`ar`、`ranlib` 等 |
| GCC | 交叉 C/C++ 编译器 |
| Glibc | 目标 C 库 |
| GMP / MPFR / MPC / ISL | GCC 依赖（链接到 gcc 源码目录） |

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

# 将依赖库链接进 GCC 源码树
cd gcc-${GCC_VER}
ln -sf ../gmp-6.3.0 gmp
ln -sf ../mpfr-4.2.1 mpfr
ln -sf ../mpc-1.3.1 mpc
ln -sf ../isl-0.27 isl
cd ..
```

### 4.3 步骤 1 — Linux 内核头文件

```bash
mkdir -p $SYSROOT/usr
cd ~/toolchain-src
make -C linux-${LINUX_VER} \
  ARCH=arm64 \
  INSTALL_HDR_PATH=$SYSROOT/usr \
  headers_install
```

安装结果：`$SYSROOT/usr/include/linux/`、`asm/` 等。

### 4.4 步骤 2 — Binutils

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

验证：

```bash
${TARGET}-as --version
${TARGET}-ld --version
```

### 4.5 步骤 3 — GCC 第一阶段（最小 C 编译器）

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
  --disable-libatomic \
  --disable-libgomp \
  --disable-libitm \
  --disable-libquadmath \
  --disable-libsanitizer \
  --disable-libssp \
  --disable-multilib \
  --with-system-zlib

make -j$JOBS all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

验证：

```bash
${TARGET}-gcc --version
${TARGET}-gcc -dumpmachine   # 应输出 aarch64-none-linux-gnu
```

### 4.6 步骤 4 — Glibc 头文件与启动文件

```bash
mkdir -p ~/toolchain-build/glibc-headers && cd ~/toolchain-build/glibc-headers

~/toolchain-src/glibc-${GLIBC_VER}/configure \
  --prefix=/usr \
  --host=$TARGET \
  --build=$BUILD \
  --with-headers=$SYSROOT/usr/include \
  --disable-multilib \
  --disable-nls \
  --enable-kernel=4.19 \
  libc_cv_forced_unwind=yes \
  libc_cv_c_cleanup=yes

make install-bootstrap-headers=yes install-headers DESTDIR=$SYSROOT
make csu/subdir_lib
install csu/crt1.o csu/crti.o csu/crtn.o $SYSROOT/usr/lib/

# 占位 libc.so，供下一阶段 GCC 链接
${TARGET}-gcc -nostdlib -nostartfiles -shared -x c /dev/null \
  -o $SYSROOT/usr/lib/libc.so
touch $SYSROOT/usr/include/gnu/stubs.h
```

### 4.7 步骤 5 — 完整 libgcc

```bash
cd ~/toolchain-build/gcc-stage1
make -j$JOBS all-target-libgcc
make install-target-libgcc
```

### 4.8 步骤 6 — 完整 Glibc

```bash
mkdir -p ~/toolchain-build/glibc && cd ~/toolchain-build/glibc

~/toolchain-src/glibc-${GLIBC_VER}/configure \
  --prefix=/usr \
  --host=$TARGET \
  --build=$BUILD \
  --with-headers=$SYSROOT/usr/include \
  --disable-multilib \
  --disable-nls \
  --enable-kernel=4.19 \
  --with-default-link \
  libc_cv_forced_unwind=yes \
  libc_cv_c_cleanup=yes

make -j$JOBS
make DESTDIR=$SYSROOT install
```

### 4.9 步骤 7 — GCC 第二阶段（最终工具链）

```bash
mkdir -p ~/toolchain-build/gcc-stage2 && cd ~/toolchain-build/gcc-stage2

~/toolchain-src/gcc-${GCC_VER}/configure \
  --prefix=$PREFIX \
  --target=$TARGET \
  --build=$BUILD \
  --host=$HOST \
  --with-sysroot=$SYSROOT \
  --enable-languages=c,c++ \
  --enable-shared \
  --enable-threads \
  --enable-tls \
  --disable-nls \
  --disable-bootstrap \
  --disable-multilib \
  --with-system-zlib

make -j$JOBS
make install
```

---

## 五、安装布局

```
$PREFIX/
├── bin/
│   ├── aarch64-none-linux-gnu-gcc
│   ├── aarch64-none-linux-gnu-g++
│   ├── aarch64-none-linux-gnu-ld
│   └── ...
└── aarch64-none-linux-gnu/
    └── sysroot/              ← 目标根文件系统
        ├── lib/
        │   └── ld-linux-aarch64.so.1
        └── usr/
            ├── include/      ← 头文件
            └── lib/          ← libc.so、libm.so 等
```

---

## 六、使用方法

将工具链加入 PATH：

```bash
export PATH=$PREFIX/bin:$PATH
```

### 编译示例

```bash
# 动态链接（需要目标机有对应 Glibc）
${TARGET}-gcc -o hello hello.c

# 静态链接（可在任意 aarch64 Linux 上运行）
${TARGET}-gcc -static -o hello_static hello.c

# C++
${TARGET}-g++ -o app app.cpp

# 指定 sysroot（交叉编译用户空间程序时推荐）
${TARGET}-gcc --sysroot=$SYSROOT -o prog prog.c
```

### 检查生成文件的架构

```bash
file hello
# hello: ELF 64-bit LSB executable, ARM aarch64, ...
```

---

## 七、常见问题

### 1. `configure: error: Building GCC requires GMP 4.2+, MPFR 3.1.0+ and MPC 0.8.0+`

确保 GMP/MPFR/MPC/ISL 已正确链接到 `gcc-${GCC_VER}/` 目录下。

### 2. Glibc 编译失败 / sanitizer 相关错误

使用 `--disable-libsanitizer`（脚本已包含），或确保 GCC 第一阶段未启用 sanitizer。

### 3. `cannot find crt1.o` 或 `cannot find -lc`

说明 Glibc 头文件/启动文件步骤未完成，或 `SYSROOT` 路径不一致。  
所有 configure 中的 `--with-sysroot` 必须指向同一个 `$SYSROOT`。

### 4. 与 `aarch64-linux-gnu-gcc`（apt 包）的区别

Ubuntu 的 `gcc-aarch64-linux-gnu` 是预编译包；本流程从源码构建，可自由选版本、打补丁、定制选项。

### 5. 不想编译 Glibc，只要 bare-metal / newlib

那是不同的目标（如 `aarch64-none-elf`），不需要 Glibc，流程更简单，但无法直接编译 Linux 用户态程序。

---

## 八、组件版本对照

| 组件 | 版本 | 说明 |
|------|------|------|
| Binutils | 2.43.1 | 2024 稳定版 |
| GCC | 14.2.0 | 2024 稳定版 |
| Glibc | 2.40 | 与 GCC 14 配套 |
| Linux | 6.12.5 | 内核 API 头文件 |

可在 `config.sh` 中修改版本号后重新下载、构建。

---

## 九、参考

- [GCC Cross-Compiler 官方说明](https://gcc.gnu.org/install/building.html)
- [Linux From Scratch — AArch64 工具链](https://www.linuxfromscratch.org/lfs/view/stable/partintro/toolchaintechnotes.html)
- [Preshing — How to Build a GCC Cross-Compiler](https://preshing.com/20141119/how-to-build-a-gcc-cross-compiler/)
