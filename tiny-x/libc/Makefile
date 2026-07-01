# libc Makefile - Build minimal C library as shared library and static archive
#
# Produces:
#   libc.so     - shared library (dynamically linked)
#   libc.a      - static archive

CC     = x86_64-linux-gnu-gcc
AS     = x86_64-linux-gnu-as
AR     = x86_64-linux-gnu-ar
LD     = x86_64-linux-gnu-ld

# -nostdinc: use ONLY our own headers.
# ./include   = libc/include/ (our stdio.h, stdlib.h, string.h, ...)
# ../include  = tiny-x/include/ (our stdint.h, stddef.h, stdbool.h, stdarg.h)
CFLAGS = -m64 \
          -nostdinc \
          -nostdlib \
          -fno-stack-protector \
          -fno-builtin \
          -fPIC \
          -Wall -Wextra \
          -O2 \
          -I./include \
          -I../include \
          -std=gnu11

BUILD  = ../build/libc

C_SRCS = \
    src/stdio.c \
    src/stdlib.c \
    src/string.c

ASM_SRCS = \
    src/crt0.S \
    src/syscall.S

C_OBJS   = $(addprefix $(BUILD)/, $(C_SRCS:.c=.o))
ASM_OBJS = $(addprefix $(BUILD)/, $(ASM_SRCS:.S=.S.o))
ALL_OBJS = $(C_OBJS) $(ASM_OBJS)

LIBC_SO = $(BUILD)/libc.so
LIBC_A  = $(BUILD)/libc.a
CRT0    = $(BUILD)/crt0.o

.PHONY: all clean

all: $(BUILD) $(LIBC_SO) $(LIBC_A) $(CRT0)

$(BUILD):
	mkdir -p $(BUILD)/src

# Shared library
$(LIBC_SO): $(C_OBJS) $(BUILD)/src/syscall.S.o
	$(LD) -m elf_x86_64 \
	      --shared \
	      -soname libc.so \
	      -o $@ \
	      $(C_OBJS) $(BUILD)/src/syscall.S.o
	@echo "libc.so built: $@"

# Static archive
$(LIBC_A): $(C_OBJS) $(BUILD)/src/syscall.S.o
	$(AR) rcs $@ $(C_OBJS) $(BUILD)/src/syscall.S.o
	@echo "libc.a built: $@"

# crt0.o (startup object)
$(CRT0): $(BUILD)/src/crt0.S.o
	cp $< $@

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/src/%.S.o: src/%.S
	@mkdir -p $(dir $@)
	$(AS) --64 -o $@ $<

clean:
	rm -rf $(BUILD)
