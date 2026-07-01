# Hello World Makefile
CC    = x86_64-linux-gnu-gcc
LD    = x86_64-linux-gnu-ld

LIBC_DIR = ../build/libc
LIBC_INC = ../libc/include

CFLAGS = -m64 \
          -nostdinc \
          -nostdlib \
          -fno-stack-protector \
          -fno-builtin \
          -fPIE \
          -O2 -Wall \
          -I$(LIBC_INC) \
          -I../include \
          -std=gnu11

BUILD  = ../build/hello
TARGET = $(BUILD)/hello

.PHONY: all clean

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): hello.c $(LIBC_DIR)/libc.so
	$(CC) $(CFLAGS) -c -o $(BUILD)/hello.o hello.c
	$(LD) -m elf_x86_64 \
	      --dynamic-linker /lib/ld-tiny.so \
	      -o $@ \
	      $(LIBC_DIR)/crt0.o \
	      $(BUILD)/hello.o \
	      $(LIBC_DIR)/libc.so
	@echo "Hello built: $@"

clean:
	rm -rf $(BUILD)
