SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifndef BUILD_DIR
ifeq ($(CURDIR),$(SRC_DIR))
BUILD_DIR := $(SRC_DIR)/build
else
BUILD_DIR := $(CURDIR)
endif
endif

ARCH     := x86_64
TARGET   := $(ARCH)-elf
CC       := $(TARGET)-gcc
LD       := $(TARGET)-ld
OBJCOPY  := $(TARGET)-objcopy

# Fall back to native cross-prefix tools when a dedicated elf toolchain is absent.
ifeq ($(shell command -v $(CC) 2>/dev/null),)
CC       := x86_64-linux-gnu-gcc
LD       := x86_64-linux-gnu-ld
OBJCOPY  := x86_64-linux-gnu-objcopy
endif

CFLAGS   := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
            -mcmodel=kernel -Wall -Wextra -O2 -I$(SRC_DIR)/kernel
LDFLAGS  := -nostdlib -T $(SRC_DIR)/linker.ld

BOOT_SRC := boot/boot.S
KERNEL_SRC := \
	kernel/main.c \
	kernel/vga.c \
	kernel/serial.c \
	kernel/keyboard.c \
	kernel/shell.c \
	kernel/fs.c \
	kernel/string.c

BOOT_OBJ := $(addprefix $(BUILD_DIR)/,$(BOOT_SRC:.S=.o))
KERNEL_OBJ := $(addprefix $(BUILD_DIR)/,$(KERNEL_SRC:.c=.o))
OBJS := $(BOOT_OBJ) $(KERNEL_OBJ)

KERNEL := $(BUILD_DIR)/kernel.elf
ISO    := $(BUILD_DIR)/tiny-sys.iso
ISO_DIR := $(BUILD_DIR)/iso

.PHONY: all clean run iso

all: $(KERNEL)

$(BUILD_DIR)/boot/%.o: $(SRC_DIR)/boot/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) $(SRC_DIR)/linker.ld
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(ISO): $(KERNEL) $(SRC_DIR)/grub/grub.cfg
	@command -v xorriso >/dev/null || { \
		echo "xorriso is required for ISO builds (install with: sudo apt install xorriso)"; \
		exit 1; \
	}
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/
	cp $(SRC_DIR)/grub/grub.cfg $(ISO_DIR)/boot/grub/
	grub-mkrescue -o $(ISO) $(ISO_DIR)

clean:
	rm -rf $(BUILD_DIR)

run: $(KERNEL)
	qemu-system-x86_64 \
		-machine type=pc \
		-m 128M \
		-serial stdio \
		-display none \
		-no-reboot \
		-kernel $(KERNEL)

iso: $(ISO)
