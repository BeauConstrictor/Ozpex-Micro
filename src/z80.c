#include <stdio.h>

#include "z80.h"

#define F_S  7
#define F_Z  6
#define F_H  4
#define F_PV 2
#define F_N  1
#define F_C  0

static word z80_pair(byte hi, byte lo) {
  return (hi << 8) | lo;
}

static void z80_error(char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

static byte z80_read(z80_cpu *cpu, word addr) {
  uint8_t dev_id = cpu->mem_map[addr];
  z80_device *dev = &cpu->devices[dev_id];
  if (!dev->read) z80_error("unmapped memory area accessed");
  return dev->read(dev->state, addr);
}

static void z80_write(z80_cpu *cpu, word addr, byte data) {
  uint8_t dev_id = cpu->mem_map[addr];
  z80_device *dev = &cpu->devices[dev_id];
  if (!dev->write) z80_error("unmapped memory area accessed");
  dev->write(dev->state, addr, data);
}

static byte z80_fetch(z80_cpu *cpu) {
  return z80_read(cpu, cpu->pc++);
}

static word z80_fetch16(z80_cpu *cpu) {
  byte lo = z80_fetch(cpu);
  byte hi = z80_fetch(cpu);
  return z80_pair(hi, lo);
}

// static void z80_nfetch(z80_cpu *cpu, byte *buf, size_t n) {
//   for (size_t i = 0; i < n; i++) {
//     buf[i] = z80_fetch(cpu);
//   }
// }

static void z80_flag(z80_cpu *cpu, uint8_t flag, bool val) {
  if (val)
    cpu->f |= (1 << flag);
  else
    cpu->f &= ~(1 << flag);
}

static bool z80_getflag(z80_cpu *cpu, uint8_t flag) {
  return cpu->f & (1 << flag);
}

bool z80_parity(uint8_t x) {
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return (~x) & 1;
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
  bool old_bit = *val & 0x80;
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

static void z80_jr(z80_cpu *cpu, bool condition) {
  int8_t offset = z80_fetch(cpu);
  if (!condition) return;
  cpu->pc += offset;
}

static void z80_or(z80_cpu *cpu, byte val) {
  cpu->a |= val;

  z80_flag(cpu, F_C,  0);
  z80_flag(cpu, F_H,  0);
  z80_flag(cpu, F_N,  0);
  z80_flag(cpu, F_Z,  cpu->a == 0);
  z80_flag(cpu, F_PV, z80_parity(cpu->a));
  z80_flag(cpu, F_S,  cpu->a >> 7);
}

static byte z80_sub(z80_cpu *cpu, byte val) {
  byte result = cpu->a - val;

  z80_flag(cpu, F_C,  cpu->a < val);
  z80_flag(cpu, F_H,  ((cpu->a & 0x0F) < (val & 0x0F)));
  z80_flag(cpu, F_N,  1);
  z80_flag(cpu, F_Z,  result == 0);
  z80_flag(cpu, F_PV, ((cpu->a ^ val) & (cpu->a ^ result) & 0x80) != 0);
  z80_flag(cpu, F_S,  result >> 7);

  return result;
}

static bool z80_execute_main(z80_cpu *cpu, byte opcode) {
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
  case 0x10:
    z80_jr(cpu, --cpu->b != 0);
    break;

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
    bool old_bit = z80_getflag(cpu, F_C);
    z80_flag(cpu, F_C, cpu->a >> 7);
    cpu->a <<= 1;
    cpu->a |= old_bit;
    z80_flag(cpu, F_N, 0);
    z80_flag(cpu, F_H, 0);
  } break;

  // jr d
  case 0x18:
    z80_jr(cpu, true);
    break;

  // add hl, de
  case 0x19:
    z80_add_16bit(cpu, &cpu->h, &cpu->l, &cpu->d, &cpu->e);
    break;

  // ld a,(de)
  case 0x1a:
    cpu->a = z80_read(cpu, z80_pair(cpu->d, cpu->e));
    break;

  // dec de
  case 0x1b:
    if (cpu->e-- == 0) cpu->d--;
    break;

  // inc e
  case 0x1c:
    z80_inc_8bit(cpu, &cpu->e);
    break;

  // dec e
  case 0x1d:
    z80_dec_8bit(cpu, &cpu->e);
    break;

  // ld e,n
  case 0x1e:
    cpu->e = z80_fetch(cpu);
    break;

  // rra
  case 0x1f: {
    bool old_bit = cpu->a & 0x01;
    cpu->a >>= 1;
    cpu->a |= z80_getflag(cpu, F_C) << 7;
    z80_flag(cpu, F_C, old_bit);
    z80_flag(cpu, F_H, 0);
    z80_flag(cpu, F_N, 0);
  } break;

  // jr nz,d
  case 0x20:
    z80_jr(cpu, !z80_getflag(cpu, F_Z));
    break;

  // ld hl,nn
  case 0x21:
    cpu->l = z80_fetch(cpu);
    cpu->h = z80_fetch(cpu);
  break;

  // ld (nn),hl
  case 0x22: {
    word addr = z80_fetch16(cpu);
    z80_write(cpu, addr,   cpu->l);
    z80_write(cpu, addr+1, cpu->h);
  } break;

  // inc hl
  case 0x23:
    if (++cpu->l == 0) cpu->h++;
    break;

  // inc h
  case 0x24:
    z80_inc_8bit(cpu, &cpu->h);
    break;

  // dec h
  case 0x25:
    z80_dec_8bit(cpu, &cpu->h);
    break;

  // ld h,n
  case 0x26:
    cpu->h = z80_fetch(cpu);
    break;

  // daa
  case 0x27:
    z80_error("bcd arithmetic is not supported");
    break;

  // jr z,d
  case 0x28:
    z80_jr(cpu, z80_getflag(cpu, F_Z));
    break;

  // add hl,hl
  case 0x29:
    z80_add_16bit(cpu, &cpu->h, &cpu->l,
                       &cpu->h, &cpu->l);
    break;

  // ld hl,(nn)
  case 0x2a:
    word addr = z80_fetch16(cpu);
    cpu->l = z80_read(cpu, addr);
    cpu->h = z80_read(cpu, addr+1);
    break;

  // dec hl
  case 0x2b:
    if (cpu->l-- == 0) cpu->h--;
    break;

  // inc l
  case 0x2c:
    z80_inc_8bit(cpu, &cpu->l);
    break;

  // dec l
  case 0x2d:
    z80_dec_8bit(cpu, &cpu->l);
    break;

  // ld l,n
  case 0x2e:
    cpu->l = z80_fetch(cpu);
    break;

  // cpl
  case 0x2f:
    cpu->a = ~cpu->a;
    break;

  // jr nc,d
  case 0x30:
    z80_jr(cpu, !z80_getflag(cpu, F_C));
    break;

  // ld a,n
  case 0x3e:
    cpu->a = z80_fetch(cpu);
    break;

  // ld (nn),a
  case 0x32: {
    z80_write(cpu, z80_fetch16(cpu), cpu->a);
  } break;

  // halt
  case 0x76:
    return false;
    break;

  // or a
  case 0xb7:
    z80_or(cpu, cpu->a);
    break;

  // out (n),a
  case 0xd3:
    cpu->io_out(z80_pair(cpu->a, z80_fetch(cpu)), cpu->a);
    break;

  // in a,(n)
  case 0xdb:
    cpu->a = cpu->io_in(z80_pair(cpu->a, z80_fetch(cpu)));
    break;

  // jp nn
  case 0xc3: {
    cpu->pc = z80_fetch16(cpu);
  } break;

  // cp n
  case 0xfe:
    z80_sub(cpu, z80_fetch(cpu));
    break;

  default:
    z80_error("unsupported instruction");
  }

  return true;
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
      if (instr.prefix_len == sizeof(instr.prefix))
        z80_error("malformed instruction");
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

bool z80_execute(z80_cpu *cpu, z80_instr *instr) {
  if (instr->prefix_len == 0) {
    return z80_execute_main(cpu, instr->opcode);
  } else {
    z80_error("unsupported instruction");
    return false;
  }
}

void z80_debug_print_flags(z80_cpu *cpu) {
  bool s  = z80_getflag(cpu, F_S);
  bool z  = z80_getflag(cpu, F_Z);
  bool h  = z80_getflag(cpu, F_H);
  bool pv = z80_getflag(cpu, F_PV);
  bool n  = z80_getflag(cpu, F_N);
  bool c  = z80_getflag(cpu, F_C);

  printf("            sign: %c\n", s ? '-' : '+');
  printf("            zero: %c= 0\n", z ? '=' : '!');
  printf("      half-carry: %c\n", h ? 'y' : 'n');
  printf(" parity/overflow: %c\n", pv ? 'y' : 'n');
  printf("         add/sub: %c\n", n ? '-' : '+');
  printf("           carry: %c\n", c ? 'y' : 'n');
}

void z80_debug_print(z80_cpu *cpu) {
  printf("pc: %04xh\n", cpu->pc);
  printf("\n");
  z80_debug_print_flags(cpu);
  printf("\n");
  printf("a: %02x f: %02x, %5u\n", cpu->a, cpu->f, z80_pair(cpu->a, cpu->f));
  printf("b: %02x c: %02x, %5u\n", cpu->b, cpu->c, z80_pair(cpu->b, cpu->c));
  printf("d: %02x e: %02x, %5u\n", cpu->d, cpu->e, z80_pair(cpu->d, cpu->e));
  printf("h: %02x l: %02x, %5u\n", cpu->h, cpu->l, z80_pair(cpu->h, cpu->l));
}
