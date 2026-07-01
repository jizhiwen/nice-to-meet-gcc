# tiny-x - Minimal x86_64 OS
# Top-level Makefile
#
# Build order:
#   1. tools  (host: tiny-as, tiny-ld)
#   2. libc   (userspace C library)
#   3. dynlink (dynamic linker ld-tiny.so)
#   4. shell  (tiny shell)
#   5. hello  (hello world)
#   6. kernel (with embedded initramfs)
#   7. iso    (bootable ISO for QEMU)

BUILD = $(CURDIR)/build
SHELL_CMD = /bin/bash

.PHONY: all clean iso run tools libc dynlink shell hello kernel boot \
        check-deps

all: check-deps tools libc dynlink shell hello kernel iso

# ── Check build dependencies ──────────────────────────────────────────
check-deps:
	@echo "=== Checking build dependencies ==="
	@which x86_64-linux-gnu-gcc >/dev/null 2>&1 || \
	    (echo "ERROR: x86_64-linux-gnu-gcc not found"; exit 1)
	@which x86_64-linux-gnu-as  >/dev/null 2>&1 || \
	    (echo "ERROR: x86_64-linux-gnu-as not found"; exit 1)
	@which x86_64-linux-gnu-ld  >/dev/null 2>&1 || \
	    (echo "ERROR: x86_64-linux-gnu-ld not found"; exit 1)
	@which grub-mkrescue >/dev/null 2>&1 || \
	    (echo "WARNING: grub-mkrescue not found, ISO creation will fail")
	@which cpio >/dev/null 2>&1 || \
	    (echo "WARNING: cpio not found, initramfs creation will fail")
	@echo "Dependencies OK"

# ── Host tools ───────────────────────────────────────────────────────
tools:
	@echo "=== Building host tools ==="
	$(MAKE) -C as
	$(MAKE) -C ld
	@echo "Tools: tiny-as, tiny-ld"

# ── Userspace libc ───────────────────────────────────────────────────
libc:
	@echo "=== Building libc ==="
	$(MAKE) -C libc

# ── Dynamic linker ────────────────────────────────────────────────────
dynlink: libc
	@echo "=== Building dynamic linker ==="
	$(MAKE) -C dynlink

# ── Shell ────────────────────────────────────────────────────────────
shell: libc
	@echo "=== Building shell ==="
	$(MAKE) -C shell

# ── Hello world ──────────────────────────────────────────────────────
hello: libc
	@echo "=== Building hello world ==="
	$(MAKE) -C hello

# ── Build initramfs (before kernel embeds it) ─────────────────────────
initramfs: shell hello dynlink libc
	@echo "=== Building initramfs ==="
	mkdir -p $(BUILD)/initramfs_stage/bin $(BUILD)/initramfs_stage/lib
	mkdir -p $(BUILD)/initramfs_stage/etc $(BUILD)/initramfs_stage/tmp
	cp $(BUILD)/shell/sh           $(BUILD)/initramfs_stage/bin/sh
	cp $(BUILD)/hello/hello        $(BUILD)/initramfs_stage/bin/hello
	cp $(BUILD)/libc/libc.so       $(BUILD)/initramfs_stage/lib/libc.so
	cp $(BUILD)/dynlink/ld-tiny.so $(BUILD)/initramfs_stage/lib/ld-tiny.so
	printf "Welcome to tiny-x!\nType 'hello' to run Hello World.\n" \
	    > $(BUILD)/initramfs_stage/etc/motd
	cd $(BUILD)/initramfs_stage && find . | sort | cpio --create --format=newc \
	    > $(BUILD)/initramfs.cpio
	@echo "initramfs.cpio built: $(shell wc -c < $(BUILD)/initramfs.cpio) bytes"

# ── Kernel ───────────────────────────────────────────────────────────
# Embed initramfs into kernel binary via .initramfs section
kernel: initramfs
	@echo "=== Building kernel ==="
	# Create initramfs object to link into kernel
	mkdir -p $(BUILD)/kernel
	@if [ -f $(BUILD)/initramfs.cpio ]; then \
	    cd $(BUILD) && \
	    x86_64-linux-gnu-objcopy \
	        --input-target binary \
	        --output-target elf64-x86-64 \
	        --binary-architecture i386:x86-64 \
	        --rename-section .data=.initramfs,alloc,load,data,contents \
	        initramfs.cpio initramfs.o && \
	    echo "initramfs.o created"; \
	fi
	$(MAKE) -C kernel
	# If initramfs.o exists, re-link kernel with it
	@if [ -f $(BUILD)/initramfs.o ]; then \
	    echo "Linking kernel with initramfs..."; \
	    x86_64-linux-gnu-ld \
	        -m elf_x86_64 \
	        -T kernel/kernel.ld \
	        --no-dynamic-linker \
	        -o $(BUILD)/kernel/kernel \
	        $(BUILD)/kernel/arch/x86_64/boot.S.o \
	        $(BUILD)/kernel/arch/x86_64/irq.S.o \
	        $(BUILD)/kernel/arch/x86_64/syscall.S.o \
	        $(BUILD)/kernel/main.o \
	        $(BUILD)/kernel/arch/x86_64/gdt.o \
	        $(BUILD)/kernel/arch/x86_64/idt.o \
	        $(BUILD)/kernel/arch/x86_64/paging.o \
	        $(BUILD)/kernel/mm/pmm.o \
	        $(BUILD)/kernel/mm/vmm.o \
	        $(BUILD)/kernel/proc/process.o \
	        $(BUILD)/kernel/proc/elf.o \
	        $(BUILD)/kernel/fs/vfs.o \
	        $(BUILD)/kernel/fs/initramfs.o \
	        $(BUILD)/kernel/drivers/vga.o \
	        $(BUILD)/kernel/drivers/keyboard.o \
	        $(BUILD)/kernel/drivers/serial.o \
	        $(BUILD)/kernel/syscall/syscalls.o \
	        $(BUILD)/kernel/lib/string.o \
	        $(BUILD)/initramfs.o 2>&1 || echo "Note: link with initramfs failed, using kernel without it"; \
	fi
	@echo "Kernel: $(BUILD)/kernel/kernel"
	@ls -lh $(BUILD)/kernel/kernel 2>/dev/null || echo "Kernel binary not found"

# ── Custom bootloader (educational) ──────────────────────────────────
boot:
	@echo "=== Building custom bootloader (educational) ==="
	mkdir -p $(BUILD)/boot
	x86_64-linux-gnu-as --32 -o $(BUILD)/boot/mbr.o boot/mbr.S
	x86_64-linux-gnu-ld -m elf_i386 --oformat binary -Ttext 0x7C00 \
	    -o $(BUILD)/boot/mbr.bin $(BUILD)/boot/mbr.o
	x86_64-linux-gnu-as --32 -o $(BUILD)/boot/stage2.o boot/stage2.S
	x86_64-linux-gnu-ld -m elf_i386 --oformat binary -Ttext 0x7E00 \
	    -o $(BUILD)/boot/stage2.bin $(BUILD)/boot/stage2.o
	@echo "MBR: $(BUILD)/boot/mbr.bin"
	@echo "Stage2: $(BUILD)/boot/stage2.bin"

# ── Create bootable ISO ───────────────────────────────────────────────
iso: kernel
	@echo "=== Creating bootable ISO ==="
	mkdir -p $(BUILD)/iso_root/boot/grub
	cp $(BUILD)/kernel/kernel $(BUILD)/iso_root/boot/kernel
	cp grub/grub.cfg          $(BUILD)/iso_root/boot/grub/grub.cfg
	@[ -f $(BUILD)/initramfs.cpio ] && \
	    cp $(BUILD)/initramfs.cpio $(BUILD)/iso_root/boot/initramfs.cpio || \
	    echo "Warning: no initramfs.cpio found"
	# Create GRUB El Torito boot image with minimal modules
	grub-mkimage \
	    --format=i386-pc-eltorito \
	    --output=$(BUILD)/iso_root/boot/grub/eltorito.img \
	    --prefix=/boot/grub \
	    iso9660 multiboot2 normal biosdisk
	# Create hybrid ISO/MBR using genisoimage
	genisoimage \
	    -R -J \
	    -b boot/grub/eltorito.img \
	    -no-emul-boot \
	    -boot-load-size 4 \
	    -boot-info-table \
	    -o $(BUILD)/tiny-x.iso \
	    $(BUILD)/iso_root \
	    2>&1 | grep -v "^$$" || true
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════╗"
	@echo "║  tiny-x.iso created: $(BUILD)/tiny-x.iso"
	@echo "║"
	@echo "║  Run with QEMU:  make run"
	@echo "║  Or manually:    qemu-system-x86_64 -cdrom $(BUILD)/tiny-x.iso \\"
	@echo "║                      -m 256M -nographic -no-reboot"
	@echo "╚══════════════════════════════════════════════════════════╝"

# ── Run in QEMU ───────────────────────────────────────────────────────
run: $(BUILD)/tiny-x.iso
	@echo "=== Booting tiny-x in QEMU ==="
	qemu-system-x86_64 \
	    -cdrom $(BUILD)/tiny-x.iso \
	    -m 256M \
	    -nographic \
	    -no-reboot

# ── Clean ─────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
	$(MAKE) -C kernel  clean 2>/dev/null || true
	$(MAKE) -C libc    clean 2>/dev/null || true
	$(MAKE) -C dynlink clean 2>/dev/null || true
	$(MAKE) -C shell   clean 2>/dev/null || true
	$(MAKE) -C hello   clean 2>/dev/null || true
	$(MAKE) -C as      clean 2>/dev/null || true
	$(MAKE) -C ld      clean 2>/dev/null || true

# ── Info ─────────────────────────────────────────────────────────────
info:
	@echo "tiny-x build system"
	@echo "Targets:"
	@echo "  all      - Build everything and create ISO"
	@echo "  tools    - Build host tools (tiny-as, tiny-ld)"
	@echo "  libc     - Build userspace C library"
	@echo "  dynlink  - Build dynamic linker (ld-tiny.so)"
	@echo "  shell    - Build tiny shell"
	@echo "  hello    - Build hello world"
	@echo "  kernel   - Build kernel"
	@echo "  iso      - Create bootable ISO"
	@echo "  run      - Boot ISO in QEMU"
	@echo "  boot     - Build custom MBR+Stage2 bootloader"
	@echo "  clean    - Remove build artifacts"
