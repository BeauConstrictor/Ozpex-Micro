CC := clang
CFLAGS := -g -Wall -Wextra -Werror -Wimplicit-fallthrough

Z80ASM := vasmz80_oldstyle
ASMFLAGS := 

.PHONY: all
all: build/ozm build/ram.bin

.PHONY: build
build/:
	mkdir -p build/

build/main.o: src/main.c src/z80.h src/ram.h src/global.h | build/
	$(CC) -c $(CFLAGS) $< -o $@

build/z80.o: src/z80.c src/z80.h src/global.h | build/
	$(CC) -c $(CFLAGS) $< -o $@

build/ram.o: src/ram.c src/ram.h src/z80.h src/global.h | build/
	$(CC) -c $(CFLAGS) $< -o $@

build/serial.o: src/serial.c src/serial.h src/z80.h src/global.h | build/
	$(CC) -c $(CFLAGS) $< -o $@

build/ozm: build/main.o build/z80.o build/ram.o build/serial.o | build/
	$(CC) $(CFLAGS) $^ -o $@

build/ram.bin: src/os/main.asm | build/
	$(Z80ASM) $(ASMFLAGS) -Fbin -dotdir -esc -o $@ $<

.PHONY: run
run: all
	build/ozm

.PHONY: dbg
dbg: all
	gdb build/ozm
