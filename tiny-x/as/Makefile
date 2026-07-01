# tiny-as Makefile (host tool)
CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11
BUILD  = ../build/as
TARGET = $(BUILD)/tiny-as

.PHONY: all clean
all: $(BUILD) $(TARGET)
$(BUILD): ; mkdir -p $(BUILD)
$(TARGET): tiny-as.c ; $(CC) $(CFLAGS) -o $@ $<
clean: ; rm -rf $(BUILD)
