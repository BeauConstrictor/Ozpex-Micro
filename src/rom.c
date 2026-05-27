#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "global.h"
#include "bios.h"
#include "z80.h"

#include "rom.h"

static void rom_err(const char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

static byte rom_read(void *state, word addr) {
  return ((byte*)state)[addr];
}

static void rom_write(void *state, word addr, byte val) {
  (void)state;
  (void)addr;
  (void)val;
  rom_err("cannot write to (read-only) rom");
}

void rom_create(z80_device *device, const char *path, size_t start, size_t size) {
  device->state = malloc(size);
  device->read = rom_read;
  device->write = rom_write;

  if (path) {
    FILE *f = fopen(path, "rb");
    if (!f) rom_err(strerror(errno));

    fseek(f, 0, SEEK_END);
    long img_size = ftell(f);
    rewind(f);
    if (img_size < 0) rom_err("failed to open rom image");
    if (img_size > (long)size) rom_err("rom image too large");

    size_t read_size = fread(device->state+start, 1, img_size, f);
    if ((long)read_size != img_size) rom_err("failed to read rom image");

    fclose(f);
  } else {
    if (build_bios_bin_len > size) rom_err("bios image too large");
    memcpy(device->state+start, build_bios_bin, build_bios_bin_len);
  }
}
