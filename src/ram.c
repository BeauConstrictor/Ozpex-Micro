#include <string.h>
#include <stdio.h>

#include "global.h"
#include "z80.h"

#include "ram.h"

static byte ram_read(void *state, word addr) {
  byte *ram = state;
  return ram[addr];
}

static void ram_write(void *state, word addr, byte val) {
  byte *ram = state;
  ram[addr] = val;
}

ssize_t ram_load_image(z80_device *ram, FILE *f) {
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  if (size < 0) return -1;
  if (size > 65536) return -2;

  size_t read_size = fread(ram->state, 1, size, f);
  if ((long)read_size != size) return -3;

  return read_size;
}

void ram_create(z80_device *ram) {
  ram->state = malloc(65536);
  memset(ram->state, 0xff, 65536);
  ram->read = ram_read;
  ram->write = ram_write;
}

