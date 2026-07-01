# Dynamic linker Makefile
CC    = x86_64-linux-gnu-gcc
LD    = x86_64-linux-gnu-ld

CFLAGS = -m64 \
          -nostdinc \
          -nostdlib \
          -fno-stack-protector \
          -fno-builtin \
          -fPIC \
          -fpie \
          -Wall -O2 \
          -I../include \
          -std=gnu11

BUILD  = ../build/dynlink
TARGET = $(BUILD)/ld-tiny.so

.PHONY: all clean

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): dynlink.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/dynlink.o dynlink.c
	$(LD) -m elf_x86_64 \
	      --shared -Bsymbolic \
	      -soname ld-tiny.so \
	      -e _start \
	      -z now \
	      -o $@ \
	      $(BUILD)/dynlink.o
	@echo "ld-tiny.so built: $@"

clean:
	rm -rf $(BUILD)
