#include <termios.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "serial.h"
#include "z80.h"
#include "ram.h"

struct termios old_termios;

void initialise_terminal() {
  struct termios raw;

  tcgetattr(STDIN_FILENO, &old_termios);
  raw = old_termios;

  raw.c_lflag &= ~(ICANON | ECHO);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void restore_terminal() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

int main() {
  initialise_terminal();
  atexit(restore_terminal);

  z80_cpu cpu = {0};

  cpu.io_in = serial_in;
  cpu.io_out = serial_out;

  const uint8_t RAM = 0;
  z80_device *ram = &cpu.devices[RAM];
  ram_create(ram);
  // cpu is zero initialised, so all addresses point to device 0
  // by default

  FILE *f = fopen("build/ram.bin", "rb");
  if (!f) {
    perror("ozm");
    return 1;
  }
  if (ram_load_image(ram, f) < 0) {
    fprintf(stderr, "ozm: failed to load ram image");
    return 1;
  }

  fclose(f);
  
  while (1) {
    z80_instr instr = z80_decode(&cpu);
    if (!z80_execute(&cpu, &instr)) break;
  }

  return 0;
}
