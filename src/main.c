#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define F_S  7
#define F_Z  6
#define F_H  4
#define F_PV 2
#define F_N  1
#define F_C  0

typedef uint8_t byte;
typedef uint16_t word;

typedef struct {
  byte (*read)(void *state, word addr);
  void (*write)(void *state, word addr, byte val);
  void *state;
} z80_device;

typedef struct {
  z80_device devices[256];
  uint8_t dev_lookup[65536]; // an index into devices; the device to
                             // use at any given memory addr
  bool halt;

  word pc;

  word af;

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

word z80_pair(byte hi, byte lo) { return (hi << 8) | lo; }

void z80_error(char *s) {
  puts(s);
  exit(1);
}

byte z80_read(z80_cpu *cpu, word addr) {
  uint8_t dev_id = cpu->dev_lookup[addr];
  z80_device *dev = &cpu->devices[dev_id];
  return dev->read(dev->state, addr);
}

void z80_write(z80_cpu *cpu, word addr, byte data) {
  uint8_t dev_id = cpu->dev_lookup[addr];
  z80_device *dev = &cpu->devices[dev_id];
  dev->write(dev->state, addr, data);
}

byte z80_fetch(z80_cpu *cpu) { return z80_read(cpu, cpu->pc++); }

void z80_nfetch(z80_cpu *cpu, byte *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    buf[i] = z80_fetch(cpu);
  }
}

z80_instr z80_decode(z80_cpu *cpu) {
  z80_instr instr = {0};
  instr.opcode = -1;

  while (1) {
    byte b = z80_fetch(cpu);
    bool is_prefix = b == 0xcb || b == 0xdd || b == 0xfd || b == 0xed;
    bool is_displacement_byte =
        instr.prefix_len == 2 && !instr.has_displacement &&
        ((instr.prefix[0] == 0xdd && instr.prefix[1] == 0xcb) ||
         (instr.prefix[0] == 0xfd && instr.prefix[1] == 0xcb));

    if (is_prefix) {
      instr.prefix[instr.prefix_len++] = b;
    } else if (is_displacement_byte) {
      instr.displacement = b;
      instr.has_displacement = true;
    } else {
      instr.opcode = b;
      break;
    }
  }

  return instr;
}

void z80_flag(z80_cpu *cpu, uint8_t flag, bool val) {
  if (val)
    cpu->f |= (1 << flag);
  else
    cpu->f &= ~(1 << flag);
}

static void z80_inc_8bit(z80_cpu *cpu, uint8_t *val) {
  z80_flag(cpu, F_H, (*val & 0x0f) == 0x0f);
  z80_flag(cpu, F_PV, *val == 0x7f);
  (*val)++;
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_Z, *val == 0x00);
  z80_flag(cpu, F_S, *val & (1 << 7));
}

static void z80_dec_8bit(z80_cpu *cpu, byte *val) {
  (*val)--;
  z80_flag(cpu, F_PV, *val == 0x7f);
  z80_flag(cpu, F_H, (*val & 0x0f) == 0x0f);
  z80_flag(cpu, F_N, 1);
  z80_flag(cpu, F_Z, *val == 0);
  z80_flag(cpu, F_S, *val & (1 << 7));
}

static void z80_rlcx(z80_cpu *cpu, byte *val) {
  bool old_bit = *val & 0x01;
  *val = *val << 1;
  *val |= old_bit;
  z80_flag(cpu, F_C, old_bit);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_H, 0);
}

static void z80_rrcx(z80_cpu *cpu, byte *val) {
  bool old_bit = *val & 0x01;
  *val = *val >> 1;
  *val |= old_bit << 7;
  z80_flag(cpu, F_C, old_bit);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_H, 0);
}

static void z80_add_16bit(z80_cpu *cpu, byte *ah, byte *al, byte *bh,
                          byte *bl) {
  word a = z80_pair(*ah, *al);
  word b = z80_pair(*bh, *bl);
  uint32_t result = a + b;

  z80_flag(cpu, F_C, result > 0xffff);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_H, ((a ^ b ^ result) & 0x1000));

  *ah = (result >> 8) & 0xff;
  *al = result & 0xff;
}

bool z80_execute_main(z80_cpu *cpu, byte opcode) {
  (void)cpu;

  switch (opcode) {
  // nop
  case 0x00:
    break;

  // ld bc, nn
  case 0x01:
    cpu->c = z80_fetch(cpu);
    cpu->b = z80_fetch(cpu);
    break;

  // ld (bc), a
  case 0x02:
    z80_write(cpu, z80_pair(cpu->b, cpu->c), cpu->a);
    break;

  // inc bc
  case 0x03:
    if (++cpu->c == 0)
      cpu->b++;
    break;

  // inc b
  case 0x04:
    z80_inc_8bit(cpu, &cpu->b);
    break;

  // dec b
  case 0x05:
    z80_dec_8bit(cpu, &cpu->b);
    break;

  // ld b, n
  case 0x06:
    cpu->b = z80_fetch(cpu);
    break;

  // rlca
  case 0x07: {
    z80_rlcx(cpu, &cpu->a);
  } break;

  // ex af, af'
  case 0x08: {
    byte orig_a_ = cpu->a_;
    byte orig_f_ = cpu->f_;
    cpu->a_ = cpu->a;
    cpu->f_ = cpu->f;
    cpu->a = orig_a_;
    cpu->f = orig_f_;
  } break;

  // add hl, bc
  case 0x09:
    z80_add_16bit(cpu, &cpu->h, &cpu->l, &cpu->b, &cpu->c);
    break;

  // ld a, (bc)
  case 0x0a:
    cpu->a = z80_read(cpu, z80_pair(cpu->b, cpu->c));
    break;

  // dec bc
  case 0x0b:
    if (cpu->c-- == 0x00)
      cpu->b--;
    break;

  // inc c
  case 0x0c:
    z80_inc_8bit(cpu, &cpu->c);
    break;

  // dec c
  case 0x0d:
    z80_dec_8bit(cpu, &cpu->c);
    break;

  // ld c, n
  case 0x0e:
    cpu->c = z80_fetch(cpu);
    break;

  // rrca
  case 0x0f:
    z80_rrcx(cpu, &cpu->a);
    break;

  // djnz d
  case 0x10: {
    int8_t offset = z80_fetch(cpu);
    if (--cpu->b != 0) {
      cpu->pc -= 2; // offset is relative to instruction start
      cpu->pc += offset;
    }
  } break;

  // ld de, nn
  case 0x11:
    cpu->e = z80_fetch(cpu);
    cpu->d = z80_fetch(cpu);
    break;

  // ld (de), a
  case 0x12:
    z80_write(cpu, z80_pair(cpu->d, cpu->e), cpu->a);
    break;

  // inc de
  case 0x13:
    if (++cpu->e == 0)
      cpu->d++;
    break;

  // inc d
  case 0x14:
    z80_inc_8bit(cpu, &cpu->d);
    break;

  // dec d
  case 0x15:
    z80_dec_8bit(cpu, &cpu->d);
    break;

  // ld d, n
  case 0x16:
    cpu->d = z80_fetch(cpu);
    break;

  // rla
  case 0x17: {
    bool old_bit = cpu->f & (1 << F_C);
    z80_flag(cpu, F_C, cpu->a >> 7);
    cpu->a <<= 1;
    cpu->a |= old_bit;
    z80_flag(cpu, F_N, 0);
    z80_flag(cpu, F_H, 0);
  } break;

  // jr d
  case 0x18: {
    int8_t offset = z80_fetch(cpu);
    cpu->pc -= 2; // offset is relative to instruction start
    cpu->pc += offset;
  } break;

  // add hl, de
  case 0x19:
    z80_add_16bit(cpu, &cpu->h, &cpu->l, &cpu->d, &cpu->e);
    break;

  // halt
  case 0x76:
    return false;
    break;

  default:
    z80_error("unsupported instruction");
  }

  return true;
}

bool z80_execute(z80_cpu *cpu, z80_instr *instr) {
  if (instr->prefix_len == 0) {
    return z80_execute_main(cpu, instr->opcode);
  } else {
    z80_error("unsupported instruction");
    return false;
  }
}

void z80_debug_print(z80_cpu *cpu) {
  printf("pc: %04x\n", cpu->pc);
  printf("f:  %04x\n",  cpu->f);
  printf("\n");
  printf("regs:\n");
  printf("a: %02x f: %02x\n", cpu->a, cpu->f);
  printf("b: %02x c: %02x\n", cpu->b, cpu->c);
  printf("d: %02x e: %02x\n", cpu->d, cpu->e);
  printf("h: %02x l: %02x\n", cpu->h, cpu->l);
}

byte ram_read(void *state, word addr) {
  byte *ram = state;
  return ram[addr];
}

void ram_write(void *state, word addr, byte val) {
  byte *ram = state;
  ram[addr] = val;
}

ssize_t load_image(byte *memory, FILE *f) {
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  if (size < 0) return -1;
  if (size > 65536) return -2;

  size_t read_size = fread(memory, 1, size, f);
  if ((long)read_size != size) return -3;

  return read_size;
}

int main() {
  z80_cpu cpu = {0};

  z80_device *ram = &cpu.devices[0];
  byte *memory = malloc(65536);
  ram->state = memory;
  ram->read = ram_read;
  ram->write = ram_write;

  for (int i = 0; i < 65536; i++)
    memory[i] = 0x76; // halt

  FILE *f = fopen("ram.bin", "rb");
  if (!f) {
    perror("ozm");
    return 1;
  }
  if (load_image(memory, f) < 0) {
    fprintf(stderr, "ozm: failed to load ram image");
    return 1;
  }

  fclose(f);

  while (1) {
    z80_instr instr = z80_decode(&cpu);
    if (!z80_execute(&cpu, &instr)) break;
  }

  z80_debug_print(&cpu);

  return 0;
}
