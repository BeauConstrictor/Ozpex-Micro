#include <termios.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "serial.h"
#include "bdev.h"
#include "z80.h"
#include "ram.h"
#include "rom.h"

#define RAM 1
#define BLOCK_DEV 2
#define ROM 3

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

static void start_ozm(z80_cpu *cpu) {
  if (DEBUG)
    printf("\033[2J");

  while (1) {
    if (DEBUG)
      printf("\033[H\033[2K");

    z80_instr instr = z80_decode(cpu);
    if (!z80_execute(cpu, &instr)) break;

    if (DEBUG) {
      printf("\n");
      for (int i = 0; i <= 0xf; i++) {
        printf("%02x ", z80_read(cpu, 0xe000 + i));
      }
      printf("\n");
      for (int i = 0; i <= 0xf; i++) {
        printf("%s ", 0xbff0 + i == cpu->sp ? "^^" : "  ");
      }
      printf("\n");

      z80_debug_print(cpu);
      usleep(100000);
    }
  }
}

static void setup_serial(z80_cpu *cpu) {
  cpu->io_in = serial_in;
  cpu->io_out = serial_out;
}

static void setup_ram(z80_cpu *cpu) {
  z80_device *device = &cpu->devices[RAM];
  ram_create(device);
}

static void setup_rom(z80_cpu *cpu) {
  z80_device *device = &cpu->devices[ROM];
  rom_create(device, "build/rom.bin", 0xe000, 0x2000);
}

static void setup_bdevs(z80_cpu *cpu) {
  z80_device *device = &cpu->devices[BLOCK_DEV];
  bdev_devs *bdevs = bdev_create(device, 0xc000, 0xc101, 0xc100, 0xc102);

  bdev_dev *xmem = bdev_create_xmem();
  bdev_install(bdevs, 0, xmem);
}

static void map_memory(z80_cpu *cpu) {
  for (int i = 0; i <= 0xffff; i++) {
    if (i >= 0x0000 && i <= 0xbfff) cpu->mem_map[i] = RAM;
    if (i >= 0xc000 && i <= 0xc0ff) cpu->mem_map[i] = BLOCK_DEV;
    if (i >= 0xe000 && i <= 0xffff) cpu->mem_map[i] = ROM;
  }
}

int main() {
  initialise_terminal();
  atexit(restore_terminal);

  z80_cpu cpu = {0};
  setup_serial(&cpu);
  setup_ram(&cpu);
  setup_rom(&cpu);
  setup_bdevs(&cpu);
  map_memory(&cpu);

  cpu.pc = 0xe000;

  start_ozm(&cpu);
 
  return 0;
}
