CC := clang
CFLAGS := -g -Wall -Wextra -Werror

.PHONY: all
all: build/ozm

.PHONY: build
build/:
	mkdir -p build/

build/ozm: src/main.c | build/
	$(CC) $(CFLAGS) $< -o $@

.PHONY: run
run: all
	build/ozm

.PHONY: dbg
dbg: all
	gdb build/ozm
