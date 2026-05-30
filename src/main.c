#include <termios.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

#include "serial.h"
#include "bdev.h"
#include "z80.h"
#include "ram.h"
#include "rom.h"

#define RAM 1
#define BLOCK_DEV 2
#define ROM 3

const char *HELP_MSG =
  "Usage: ozm [OPTION]...\n"
  "An Ozpex Micro emulator.\n"
  "Example: ozm -m bdsk:os-m.bin@0\n"
  "\n"
  "Options:\n"
  "  -h, --help                            Show this help text and exit\n"
  "  -v, --version                         Show version information and exit\n"
  "  -b, --bios <IMAGE>                    Use an alternative BIOS image\n"
  "  -m, --mount <DEVICE>[:<ARG>]@<0xSLOT> Use an alternative BIOS image\n"
  "\n"
  "Devices:\n"
  "  bdsk <IMAGE>                          A disk image, marked as bootable\n"
  "  disk <IMAGE>                          A disk image\n"
  "  xmem                                  A 64K extended memory module\n"
;

const char *VERSION_MSG = 
  "ozm (Ozpex Micro Emulator) " VERSION "\n"
  "Copyright (c) 2026 Beau Constrictor\n"
  "License GPLv2: GNU GPL version 2 <https://gnu.org/licenses/gpl.html>.\n"
  "This is free software: you are free to change and redistribute it.\n"
  "There is NO WARRANTY, to the extent permitted by law.\n"
  "\n"
  "Written by Beau Constrictor.\n";

struct termios old_termios;

static void initialise_terminal() {
  struct termios raw;

  tcgetattr(STDIN_FILENO, &old_termios);
  raw = old_termios;

  raw.c_lflag &= ~(ICANON | ECHO);
tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void restore_terminal() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

static void error(const char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

void start_ozm(z80_cpu *cpu) {
  if (DEBUG)
    printf("\033[2J");

  while (1) {
    if (DEBUG) {
      printf("\033[H\033[2K\n");

      for (int i = 0; i <= 0xf; i++) {
        printf("%02x ", z80_read(cpu, 0xe000 + i));
      }
      printf("\n\n");

      z80_debug_print(cpu);

      usleep(100000);
    }

    z80_instr instr = z80_decode(cpu);
    if (!z80_execute(cpu, &instr)) break;

    }
}

typedef struct {
    char id[64];
    char *path;
    unsigned int slot;
} mount;

int parse_mount(char *input, mount *out) {
    if (!input || !out) return -1;

    char *at = strchr(input, '@');
    if (!at) return -1;

    *at = '\0';
    char *rhs = at + 1;

    char *endptr;
    errno = 0;
    unsigned long v = strtoul(rhs, &endptr, 16);
    if (errno != 0 || *endptr != '\0' || v > 0xFF) {
        return -1;
    }

    out->slot = (unsigned int)v;

    char *lhs = input;
    char *colon = strchr(lhs, ':');

    if (colon) {
        *colon = '\0';
        out->path = colon + 1;
    } else {
        out->path = NULL;
    }

    if (*lhs == '\0') return -1;

    size_t len = strlen(lhs);
    if (len >= sizeof(out->id)) return -1;

    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)lhs[i])) {
            return -1;
        }
    }

    strcpy(out->id, lhs);

    return 0;
}

static void install_bdev(bdev_devs *devs, const char *id,
    const char *path, unsigned int slot) {

  if (strcmp(id, "xm") == 0) {
    bdev_dev *xmem = bdev_create_xmem();
    bdev_install(devs, slot, xmem);
  }

  else if (strcmp(id, "disk") == 0 || strcmp(id, "bdsk") == 0) {
    bool bootable = strcmp(id, "bdsk") == 0;

    if (!path)
      error("sectored storage devices require a disk image path");
    FILE *f = fopen(path, "r+b");
    if (!f)
      error("cannot open disk image");
    bdev_dev *sectd = bdev_create_sectd(bootable, f);
    if (!sectd)
      error("disk image is not 65536 bytes in size");
    bdev_install(devs, slot, sectd);
  }

  else {
    error("unknown device type");
  }
}

static void setup_system(z80_cpu *cpu, int argc, char **argv) {
  int opt;

  cpu->io_in = serial_in;
  cpu->io_out = serial_out;

  z80_device *ram = &cpu->devices[RAM];
  ram_create(ram);

  z80_device *_bdevs = &cpu->devices[BLOCK_DEV];
  bdev_devs *bdevs = bdev_create(_bdevs, 0xc000, 0xc101, 0xc100,
      0xc102);
  (void)bdevs;

  char *bios = NULL;

  static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {"bios",    required_argument, 0, 'b'},
        {"mount",   required_argument, 0, 'm'},
        {0, 0, 0, 0}
  };

  while ((opt = getopt_long(argc, argv, "hvb:m:", long_options, NULL)) != -1) {
    switch (opt) {
    case 'h':
      printf("%s", HELP_MSG);
      exit(0);

    case 'v':
      printf("%s", VERSION_MSG);
      exit(0);

    case 'b':
      bios = optarg;
      break;

    case 'm': {
      mount m;
      parse_mount(optarg, &m);
      install_bdev(bdevs, m.id, m.path, m.slot);
    } break;

    default:
        error("Try 'ozm --help' for more information.\n");
    }
  }

  z80_device *rom = &cpu->devices[ROM];
  rom_create(rom, bios, 0xe000, 0x2000);

  for (int i = 0; i <= 0xffff; i++) {
    if (i >= 0x0000 && i <= 0xbfff) cpu->mem_map[i] = RAM;
    if (i >= 0xc000 && i <= 0xc102) cpu->mem_map[i] = BLOCK_DEV;
    if (i >= 0xe000 && i <= 0xffff) cpu->mem_map[i] = ROM;
  }
}

int main(int argc, char **argv) {
  initialise_terminal();
  atexit(restore_terminal);

  z80_cpu cpu = {0};

  setup_system(&cpu, argc, argv);

  cpu.pc = 0xe000;
  start_ozm(&cpu);
 
  return 0;
}
