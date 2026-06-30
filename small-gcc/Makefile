SHELL := /usr/bin/env bash

ROOT := $(CURDIR)
BUILD := $(ROOT)/build
TOOLS := $(BUILD)/tools
OBJ := $(BUILD)/obj
BIN := $(BUILD)/bin
RUNTIME_OBJ := $(BUILD)/runtime
LIB_OBJ := $(BUILD)/libs
MAP := $(BUILD)/map

EXAMPLES := hello fib vec_demo
RUNTIME_OBJECTS := $(RUNTIME_OBJ)/crt0.o $(LIB_OBJ)/libc_tiny.o $(LIB_OBJ)/libcxx_tiny.o

.PHONY: all tools runtime libs examples check clean

all: tools runtime libs examples

tools: $(TOOLS)/tinycc $(TOOLS)/tinyas $(TOOLS)/tinyld

runtime: $(RUNTIME_OBJ)/crt0.o

libs: $(LIB_OBJ)/libc_tiny.o $(LIB_OBJ)/libcxx_tiny.o

examples: $(EXAMPLES:%=$(BIN)/%)

$(TOOLS)/tinycc: cmd/tinycc.py | $(TOOLS)
	cp "$<" "$@"
	chmod +x "$@"

$(TOOLS)/tinyas: cmd/tinyas.py | $(TOOLS)
	cp "$<" "$@"
	chmod +x "$@"

$(TOOLS)/tinyld: cmd/tinyld.py | $(TOOLS)
	cp "$<" "$@"
	chmod +x "$@"

$(RUNTIME_OBJ)/crt0.o: runtime/crt/crt0.s $(TOOLS)/tinyas | $(RUNTIME_OBJ)
	$(TOOLS)/tinyas "$<" -o "$@"

$(LIB_OBJ)/libc_tiny.o: libc_tiny/libc_tiny.s $(TOOLS)/tinyas | $(LIB_OBJ)
	$(TOOLS)/tinyas "$<" -o "$@"

$(OBJ)/libcxx_tiny.s: libcxx_tiny/libcxx_tiny.tcpp $(TOOLS)/tinycc | $(OBJ)
	$(TOOLS)/tinycc "$<" -o "$@"

$(LIB_OBJ)/libcxx_tiny.o: $(OBJ)/libcxx_tiny.s $(TOOLS)/tinyas | $(LIB_OBJ)
	$(TOOLS)/tinyas "$<" -o "$@"

$(OBJ)/%.s: examples/%.tcpp $(TOOLS)/tinycc | $(OBJ)
	$(TOOLS)/tinycc "$<" -o "$@"

$(OBJ)/%.o: $(OBJ)/%.s $(TOOLS)/tinyas | $(OBJ)
	$(TOOLS)/tinyas "$<" -o "$@"

$(BIN)/%: $(OBJ)/%.o $(RUNTIME_OBJECTS) $(TOOLS)/tinyld | $(BIN) $(MAP)
	$(TOOLS)/tinyld -o "$@" --map "$(MAP)/$*.map" "$<" $(RUNTIME_OBJECTS)

check: all
	./scripts/check.sh

clean:
	rm -rf "$(BUILD)"

$(TOOLS) $(OBJ) $(BIN) $(RUNTIME_OBJ) $(LIB_OBJ) $(MAP):
	mkdir -p "$@"
