#ifndef Z80_H
#define Z80_H

#include <stdbool.h>
#include <stdlib.h>

#include "global.h"

typedef struct {
  byte (*read)(void *state, word addr);
  void (*write)(void *state, word addr, byte val);
  void *state;
} z80_device;

typedef struct {
  z80_device devices[256];
  uint8_t mem_map[65536]; // an index into devices; the device to
                             // use at any given memory addr
  bool halt;

  byte (*io_in)(word port);
  void (*io_out)(word port, byte addr);

  word pc;
  word sp;

  byte  a, f;
  byte  b, c;
  byte  d, e;
  byte  h, l;

  byte a_, f_;
  byte b_, c_;
  byte d_, e_;
  byte h_, l_;
} z80_cpu;

typedef struct {
  size_t prefix_len;     // may be 0
  byte prefix[2];
  int8_t displacement;   // optional
  bool has_displacement;
  int opcode;            // mandatory
} z80_instr;

byte z80_read(z80_cpu *cpu, word addr);
void z80_write(z80_cpu *cpu, word addr, byte val);

z80_instr z80_decode(z80_cpu *cpu);
bool z80_execute(z80_cpu *cpu, z80_instr *instr);
void z80_debug_print(z80_cpu *cpu);

#endif // Z80_H
