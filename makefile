CC := clang
CFLAGS := -g -Wall -Wextra -Werror -Wimplicit-fallthrough

Z80ASM := vasmz80_oldstyle
ASMFLAGS := 

ASMFILES := $(wildcard src/bios/*.asm)

.PHONY: all
all: build/ozm build/rom.bin

.PHONY: build
build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) -c $(CFLAGS) -MMD -MP $< -o $@

build/ozm: build/main.o build/z80.o build/ram.o build/rom.o build/serial.o build/bdev.o | build
	$(CC) $(CFLAGS) $^ -o $@

build/rom.bin: $(ASMFILES) | build
	$(Z80ASM) $(ASMFLAGS) -Fbin -dotdir -esc -o $@ src/bios/main.asm

.PHONY: run
run: all
	build/ozm

.PHONY: dbg
dbg: all
	gdb build/ozm

.PHONY: clean
clean:
	rm -rf build

-include $(wildcard build/*.d)
