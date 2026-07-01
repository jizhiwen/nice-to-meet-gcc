# tiny-ld Makefile (host tool)
CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11
BUILD  = ../build/ld
TARGET = $(BUILD)/tiny-ld

.PHONY: all clean
all: $(BUILD) $(TARGET)
$(BUILD): ; mkdir -p $(BUILD)
$(TARGET): tiny-ld.c ; $(CC) $(CFLAGS) -o $@ $<
clean: ; rm -rf $(BUILD)
