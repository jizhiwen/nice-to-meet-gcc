# Kernel Makefile
#
# Cross-compiles a freestanding x86_64 kernel.
# No standard library, no red zone, kernel code model.

CC      = x86_64-linux-gnu-gcc
AS      = x86_64-linux-gnu-as
LD      = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

# -nostdinc: use ONLY our own headers, never GCC/glibc system headers.
# ../include  = tiny-x/include/ (our stdint.h, stddef.h, stdbool.h, stdarg.h)
# .           = kernel/         (kernel source tree root)
# ./include   = kernel/include/ (kernel-private headers: types.h, string.h)
CFLAGS  = -m64 \
           -ffreestanding \
           -fno-stack-protector \
           -fno-builtin \
           -fno-pic \
           -mno-red-zone \
           -mcmodel=large \
           -nostdinc \
           -nostdlib \
           -Wall -Wextra \
           -O2 \
           -I. \
           -I./include \
           -I../include \
           -std=gnu11

ASFLAGS = --64
# Use GCC to compile .S files so C preprocessor (#define) is applied
GCC_AS_FLAGS = -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
               -nostdlib -nostdinc -fno-builtin -c

LDFLAGS = -m elf_x86_64 \
           -T kernel.ld \
           --no-dynamic-linker

BUILD   = ../build/kernel
KERNEL  = $(BUILD)/kernel

# ── Source files ──────────────────────────────────────────────────────
ASM_SRCS = \
    arch/x86_64/boot.S \
    arch/x86_64/irq.S \
    arch/x86_64/syscall.S

C_SRCS = \
    main.c \
    arch/x86_64/gdt.c \
    arch/x86_64/idt.c \
    arch/x86_64/paging.c \
    mm/pmm.c \
    mm/vmm.c \
    proc/process.c \
    proc/elf.c \
    fs/vfs.c \
    fs/initramfs.c \
    drivers/vga.c \
    drivers/keyboard.c \
    drivers/serial.c \
    syscall/syscalls.c \
    lib/string.c

# Object files
ASM_OBJS = $(addprefix $(BUILD)/, $(ASM_SRCS:.S=.S.o))
C_OBJS   = $(addprefix $(BUILD)/, $(C_SRCS:.c=.o))
ALL_OBJS = $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean

all: $(BUILD) $(KERNEL)

$(BUILD):
	mkdir -p $(BUILD)/arch/x86_64 $(BUILD)/mm $(BUILD)/proc \
	           $(BUILD)/fs $(BUILD)/drivers $(BUILD)/syscall $(BUILD)/lib

$(KERNEL): $(ALL_OBJS) kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "Kernel: $@ ($(shell wc -c < $@) bytes)"

# Compile .S (assembly) - use GCC so #define macros are preprocessed
$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(GCC_AS_FLAGS) -o $@ $<

# Compile .c (C source)
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)
