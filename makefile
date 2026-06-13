CC := clang
CFLAGS := -g -Wall -Wextra -Werror -Wimplicit-fallthrough

Z80ASM := vasmz80_oldstyle
ASMFLAGS := 

ASMFILES := $(wildcard src/bios/*.asm)
HEADERS := $(wildcard src/*.h)

.PHONY: all
all: build/ozm

.PHONY: build
build:
	mkdir -p build

build/%.o: src/%.c $(HEADERS) | build
	$(CC) -c $(CFLAGS) -MMD -MP -Ibuild $< -o $@

build/rom.o: src/rom.c build/bios.h $(HEADERS) | build
	$(CC) -c $(CFLAGS) -MMD -MP -Ibuild $< -o $@

build/ozm: build/main.o build/z80.o build/ram.o build/rom.o build/serial.o build/bdev.o | build
	$(CC) $(CFLAGS) $^ -o $@

build/bios.bin: $(ASMFILES) | build
	$(Z80ASM) $(ASMFLAGS) -Fbin -dotdir -esc -o $@ src/bios/main.asm

build/bios.h: build/bios.bin | build
	xxd -i $< > $@

.PHONY: run
run: all
	build/ozm -m xmem@00 -m bdsk:testdisk.bin@01

.PHONY: dbg
dbg: all
	gdb build/ozm

.PHONY: clean
clean:
	rm -rf build
