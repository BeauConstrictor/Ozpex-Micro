#include <stdlib.h>
#include <stdio.h>

#include "z80.h"

#define F_S  7
#define F_Z  6
#define F_H  4
#define F_PV 2
#define F_N  1
#define F_C  0

#define AF(cpu) ( z80_pair(cpu->a, cpu->f) )
#define BC(cpu) ( z80_pair(cpu->b, cpu->c) )
#define DE(cpu) ( z80_pair(cpu->d, cpu->e) )
#define HL(cpu) ( z80_pair(cpu->h, cpu->l) )

#define SWAP(x,y) do { int tmp = x; x = y; y = tmp; } while (0)

// NOTE:
// throughout this code, the bool type is used to represent a bit,
// where i don't explicitly do a x == 0 ? 0 : 1, as bools implicitly
// do this conversion automatically.

static word z80_pair(byte hi, byte lo) {
  return ((word)hi << 8) | lo;
}

static void z80_error(char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

byte z80_read(z80_cpu *cpu, word addr) {
  uint8_t dev_id = cpu->mem_map[addr];
  z80_device *dev = &cpu->devices[dev_id];
  if (!dev->read) z80_error("unmapped memory area accessed");
  return dev->read(dev->state, addr);
}

void z80_write(z80_cpu *cpu, word addr, byte data) {
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
    uint8_t old = *val;
    uint8_t res = old + 1;
    z80_flag(cpu, F_H, (old & 0x0F) == 0x0F);
    z80_flag(cpu, F_PV, old == 0x7F);
    z80_flag(cpu, F_N, 0);
    z80_flag(cpu, F_Z, res == 0x00);
    z80_flag(cpu, F_S, res & 0x80);
    *val = res;
}

static void z80_dec_8bit(z80_cpu *cpu, uint8_t *val) {
    uint8_t old = *val;
    uint8_t res = old - 1;
    z80_flag(cpu, F_H, (old & 0x0F) == 0x00);
    z80_flag(cpu, F_PV, old == 0x80);
    z80_flag(cpu, F_N, 1);
    z80_flag(cpu, F_Z, res == 0x00);
    z80_flag(cpu, F_S, res & 0x80);
    *val = res;
}

static void z80_rlcx(z80_cpu *cpu, byte *val) {
  bool old_bit = *val & 0x80;
  *val = *val << 1;
  z80_flag(cpu, F_C, old_bit);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_H, 0);
}

static void z80_rrcx(z80_cpu *cpu, byte *val) {
  bool old_bit = *val & 0x01;
  *val = *val >> 1;
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
  z80_flag(cpu, F_H, ((a & 0x0FFF) + (b & 0x0FFF)) > 0x0FFF);

  *ah = (result >> 8) & 0xff;
  *al = result & 0xff;
}

static void z80_jr(z80_cpu *cpu, bool condition) {
  int8_t offset = z80_fetch(cpu);
  if (!condition) return;
  cpu->pc += offset;
}

static void z80_jp(z80_cpu *cpu, word addr, bool condition) {
  if (!condition) return;
  cpu->pc = addr;
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

static byte z80_cp(z80_cpu *cpu, byte val) {
  byte result = cpu->a - val;

  z80_flag(cpu, F_C,  cpu->a < val);
  z80_flag(cpu, F_H,  ((cpu->a & 0x0F) < (val & 0x0F)));
  z80_flag(cpu, F_N,  1);
  z80_flag(cpu, F_Z,  result == 0);
  z80_flag(cpu, F_PV, ((cpu->a ^ val) & (cpu->a ^ result) & 0x80) != 0);
  z80_flag(cpu, F_S,  result >> 7);

  return result;
}

static void z80_inc_indirect(z80_cpu *cpu, word addr) {
  byte val = z80_read(cpu, addr);
  z80_inc_8bit(cpu, &val);
  z80_write(cpu, addr, val);
}

static void z80_dec_indirect(z80_cpu *cpu, word addr) {
  byte val = z80_read(cpu, addr);
  z80_dec_8bit(cpu, &val);
  z80_write(cpu, addr, val);
}

static void z80_add_8bit(z80_cpu *cpu, byte b, bool c) {
  byte a = cpu->a;
  byte r = a + b + c;
  z80_flag(cpu, F_S, (r & 0x80) != 0);
  z80_flag(cpu, F_Z, r == 0);
  z80_flag(cpu, F_H, ((a & 0x0F) + (b & 0x0F) + c) > 0x0F);
  z80_flag(cpu, F_PV, (~(a ^ b) & (a ^ r) & 0x80) != 0);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_C,
      (a + b + c) > 0xFF);
  cpu->a = r;
}

static void z80_adc_8bit(z80_cpu *cpu, byte b) {
  z80_add_8bit(cpu, b, z80_getflag(cpu, F_C));
}

static void z80_sub_8bit(z80_cpu *cpu, byte b, bool borrow_in) {
  byte a = cpu->a;
  byte c = borrow_in ? 1 : 0;

  byte r = a - b - c;

  z80_flag(cpu, F_S, (r & 0x80) != 0);
  z80_flag(cpu, F_Z, r == 0);

  z80_flag(cpu, F_H,
      (a & 0x0F) < ((b & 0x0F) + c));

  z80_flag(cpu, F_PV,
      ((a ^ b) & (a ^ r) & 0x80) != 0);

  z80_flag(cpu, F_N, 1);

  z80_flag(cpu, F_C,
      a < (b + c));

  cpu->a = r;
}

static void z80_sbc_8bit(z80_cpu *cpu, byte b) {
  z80_sub_8bit(cpu, b, z80_getflag(cpu, F_C));
}

static void z80_push(z80_cpu *cpu, byte val) {
  z80_write(cpu, --cpu->sp, val);
}

static void z80_push16(z80_cpu *cpu, word val) {
  z80_push(cpu, val >> 8);
  z80_push(cpu, val & 0xff);
}

static byte z80_pop(z80_cpu *cpu) {
  return z80_read(cpu, cpu->sp++);
}

static word z80_pop16(z80_cpu *cpu) {
  byte lo = z80_pop(cpu);
  byte hi = z80_pop(cpu);
  return z80_pair(hi, lo);
}

static void z80_and(z80_cpu *cpu, byte val) {
  byte r = cpu->a & val;
  z80_flag(cpu, F_C, 0);
  z80_flag(cpu, F_N, 0);
  z80_flag(cpu, F_H, 1);
  z80_flag(cpu, F_PV, z80_parity(r));
  z80_flag(cpu, F_Z, r == 0);
  z80_flag(cpu, F_S, r >> 7);
  cpu->a = r;
}

static void z80_sub_16bit(z80_cpu *cpu, word b, bool borrow_in) {
  word a = HL(cpu);
  word c = borrow_in ? 1 : 0;

  word r = a - b - c;

  z80_flag(cpu, F_S, (r & 0x8000) != 0);
  z80_flag(cpu, F_Z, r == 0);

  z80_flag(cpu, F_H,
      (a & 0x0FFF) < ((b & 0x0FFF) + c));

  z80_flag(cpu, F_PV,
      ((a ^ b) & (a ^ r) & 0x8000) != 0);

  z80_flag(cpu, F_N, 1);

  z80_flag(cpu, F_C,
      a < (b + c));

  cpu->h = r >> 8;
  cpu->l = r & 0xff;
}

static void z80_sbc_16bit(z80_cpu *cpu, word b) {
  z80_sub_16bit(cpu, b, z80_getflag(cpu, F_C));
}

static void z80_ret(z80_cpu *cpu, bool condition) {
  if (condition)
    cpu->pc = z80_pop16(cpu);
}

static void z80_xor(z80_cpu *cpu, byte val) {
  cpu->a ^= val;
  z80_flag(cpu, F_C,  false);
  z80_flag(cpu, F_N,  false);
  z80_flag(cpu, F_PV, z80_parity(cpu->a));
  z80_flag(cpu, F_H,  false);
  z80_flag(cpu, F_Z,  cpu->a == 0);
  z80_flag(cpu, F_S,  cpu->a >> 7);
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
    if (++cpu->c == 0) cpu->b++;
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
  case 0x2a: {
    word addr = z80_fetch16(cpu);
    cpu->l = z80_read(cpu, addr);
    cpu->h = z80_read(cpu, addr+1);
  } break;

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
    z80_flag(cpu, F_N, 1);
    z80_flag(cpu, F_H, 1);
    break;

  // jr nc,d
  case 0x30:
    z80_jr(cpu, !z80_getflag(cpu, F_C));
    break;

  // ld sp,nn
  case 0x31:
    cpu->sp = z80_fetch16(cpu);
    break;

  // ld (nn),a
  case 0x32:
    z80_write(cpu, z80_fetch16(cpu), cpu->a);
    break;

  // inc sp
  case 0x33:
    cpu->sp++;
    break;

  // inc (hl)
  case 0x34: {
    word addr = HL(cpu);
    z80_inc_indirect(cpu, addr);
  } break;

  // dec (hl)
  case 0x35: {
    word addr = HL(cpu);
    z80_dec_indirect(cpu, addr);
  } break;

  // ld (hl),n
  case 0x36:
    z80_write(cpu, HL(cpu), z80_fetch(cpu));
    break;

  // scf
  case 0x37:
    z80_flag(cpu, F_C, 1);
    z80_flag(cpu, F_H, 0);
    z80_flag(cpu, F_N, 0);
    break;

  // jr c,d
  case 0x38:
    z80_jr(cpu, z80_getflag(cpu, F_C));
    break;

  // add hl,sp
  case 0x39: {
    // splitting isn't necessary, but the add function doesn't take
    // 16-bit args :/
    byte lo = cpu->sp & 0xff;
    byte hi = cpu->sp >> 8;
    z80_add_16bit(cpu, &cpu->h, &cpu->l, &hi, &lo);
    cpu->sp = z80_pair(hi, lo);
  } break;

  // ld a,(nn)
  case 0x3a:
    cpu->a = z80_read(cpu, z80_fetch16(cpu));
    break;

  // dec sp
  case 0x3b:
    cpu->sp--;
    break;

  // inc a
  case 0x3c:
    z80_inc_8bit(cpu, &cpu->a);
    break;

  // deca
  case 0x3d:
    z80_dec_8bit(cpu, &cpu->a);
    break;

  // ld a,n
  case 0x3e:
    cpu->a = z80_fetch(cpu);
    break;

  // ccf
  case 0x3f:
    z80_flag(cpu, F_H, z80_getflag(cpu, F_C));
    z80_flag(cpu, F_C, !z80_getflag(cpu, F_C));
    z80_flag(cpu, F_N, 0);
    break;

  // ld b,b
  case 0x40:
    break;

  // ld b,c
  case 0x41:
    cpu->b = cpu->c;
    break;

  // ld b,d
  case 0x42:
    cpu->b = cpu->d;
    break;

  // ld b,e
  case 0x43:
    cpu->b = cpu->e;
    break;

  // ld b,h
  case 0x44:
    cpu->b = cpu->h;
    break;

  // ld b,l
  case 0x45:
    cpu->b = cpu->l;
    break;

  // ld b,(hl)
  case 0x46:
    cpu->b = z80_read(cpu, HL(cpu));
    break;

  // ld b,a
  case 0x47:
    cpu->b = cpu->a;
    break;

  // ld c,b
  case 0x48:
    cpu->c = cpu->b;
    break;

  // ld c,c
  case 0x49:
    break;

  // ld c,d
  case 0x4a:
    cpu->c = cpu->d;
    break;

  // ld c,e
  case 0x4b:
    cpu->c = cpu->e;
    break;

  // ld c,h
  case 0x4c:
    cpu->c = cpu->h;
    break;

  // ld c,l
  case 0x4d:
    cpu->c = cpu->l;
    break;

  // ld c,(hl)
  case 0x4e:
    cpu->c = z80_read(cpu, HL(cpu));
    break;

  // ld c,a
  case 0x4f:
    cpu->c = cpu->a;
    break;

  // ld d,b
  case 0x50:
    cpu->d = cpu->b;
    break;

  // ld d,c
  case 0x51:
    cpu->d = cpu->c;
    break;

  // ld d,d
  case 0x52:
    break;

  // ld d,e
  case 0x53:
    cpu->d = cpu->e;
    break;

  // ld d,h
  case 0x54:
    cpu->d = cpu->h;
    break;

  // ld d,l
  case 0x55:
    cpu->d = cpu->l;
    break;

  // ld d,(hl)
  case 0x56:
    cpu->d = z80_read(cpu, HL(cpu));
    break;

  // ld d,a
  case 0x57:
    cpu->d = cpu->a;
    break;

  // ld e,b
  case 0x58:
    cpu->e = cpu->b;
    break;

  // ld e,c
  case 0x59:
    cpu->e = cpu->c;
    break;

  // ld e,d
  case 0x5a:
    cpu->e = cpu->d;
    break;

  // ld e,e
  case 0x5b:
    break;

  // ld e,h
  case 0x5c:
    cpu->e = cpu->h;
    break;

  // ld e,l
  case 0x5d:
    cpu->e = cpu->l;
    break;

  // ld e,(hl)
  case 0x5e:
    cpu->e = z80_read(cpu, HL(cpu));
    break;

  // ld e,a
  case 0x5f:
    cpu->e = cpu->a;
    break;

  // ld h,b
  case 0x60:
    cpu->h = cpu->b;
    break;

  // ld h,c
  case 0x61:
    cpu->h = cpu->c;
    break;

  // ld h,d
  case 0x62:
    cpu->h = cpu->d;
    break;

  // ld h,e
  case 0x63:
    cpu->h = cpu->e;
    break;

  // ld h,h
  case 0x64:
    break;

  // ld h,l
  case 0x65:
    cpu->h = cpu->l;
    break;

  // ld h,(hl)
  case 0x66:
    cpu->h = z80_read(cpu, HL(cpu));
    break;

  // ld h,a
  case 0x67:
    cpu->h = cpu->a;
    break;

  // ld l,b
  case 0x68:
    cpu->l = cpu->b;
    break;

  // ld l,c
  case 0x69:
    cpu->l = cpu->c;
    break;

  // ld l,d
  case 0x6a:
    cpu->l = cpu->d;
    break;

  // ld l,e
  case 0x6b:
    cpu->l = cpu->e;
    break;

  // ld l,h
  case 0x6c:
    cpu->l = cpu->h;
    break;

  // ld l,l
  case 0x6d:
    break;

  // ld l,(hl)
  case 0x6e:
    cpu->l = z80_read(cpu, HL(cpu));
    break;

  // ld l,a
  case 0x6f:
    cpu->l = cpu->a;
    break;

  // ld (hl),b
  case 0x70:
    z80_write(cpu, HL(cpu), cpu->b);
    break;

  // ld (hl),c
  case 0x71:
    z80_write(cpu, HL(cpu), cpu->c);
    break;

  // ld (hl),d
  case 0x72:
    z80_write(cpu, HL(cpu), cpu->d);
    break;

  // ld (hl),e
  case 0x73:
    z80_write(cpu, HL(cpu), cpu->e);
    break;

  // ld (hl),h
  case 0x74:
    z80_write(cpu, HL(cpu), cpu->h);
    break;

  // ld (hl),l
  case 0x75:
    z80_write(cpu, HL(cpu), cpu->l);
    break;

  // halt
  case 0x76:
    return false;

  // ld (hl),a
  case 0x77:
    z80_write(cpu, HL(cpu), cpu->a);
    break;

  // ld a,b
  case 0x78:
    cpu->a = cpu->b;
    break;

  // ld a,c
  case 0x79:
    cpu->a = cpu->c;
    break;

  // ld a,d
  case 0x7a:
    cpu->a = cpu->d;
    break;

  // ld a,e
  case 0x7b:
    cpu->a = cpu->e;
    break;

  // ld a,h
  case 0x7c:
    cpu->a = cpu->h;
    break;

  // ld a,l
  case 0x7d:
    cpu->a = cpu->l;
    break;

  // ld a,(hl)
  case 0x7e:
    cpu->a = z80_read(cpu, HL(cpu));
    break;

  // ld a,a
  case 0x7f:
    break;

  // add a,b
  case 0x80:
    z80_add_8bit(cpu, cpu->b, 0);
    break;

  // add a,c
  case 0x81:
    z80_add_8bit(cpu, cpu->c, 0);
    break;

  // add a,d
  case 0x82:
    z80_add_8bit(cpu, cpu->d, 0);
    break;

  // add a,e
  case 0x83:
    z80_add_8bit(cpu, cpu->e, 0);
    break;

  // add a,h
  case 0x84:
    z80_add_8bit(cpu, cpu->h, 0);
    break;

  // add a,l
  case 0x85:
    z80_add_8bit(cpu, cpu->l, 0);
    break;

  // add a,(hl)
  case 0x86:
    z80_add_8bit(cpu, z80_read(cpu, HL(cpu)), 0);
    break;

  // add a,a
  case 0x87:
    z80_add_8bit(cpu, cpu->a, 0);
    break;

  // adc a,b
  case 0x88:
    z80_adc_8bit(cpu, cpu->b);
    break;

  // adc a,c
  case 0x89:
    z80_adc_8bit(cpu, cpu->c);
    break;

  // adc a,d
  case 0x8a:
    z80_adc_8bit(cpu, cpu->d);
    break;

  // adc a,e
  case 0x8b:
    z80_adc_8bit(cpu, cpu->e);
    break;

  // adc a,h
  case 0x8c:
    z80_adc_8bit(cpu, cpu->h);
    break;

  // adc a,l
  case 0x8d:
    z80_adc_8bit(cpu, cpu->l);
    break;

  // adc a,(hl)
  case 0x8e:
    z80_adc_8bit(cpu, z80_read(cpu, HL(cpu)));
    break;

  // adc a,a
  case 0x8f:
    z80_adc_8bit(cpu, cpu->a);
    break;

  // sub b
  case 0x90:
    z80_sub_8bit(cpu, cpu->b, 0);
    break;

  // sub c
  case 0x91:
    z80_sub_8bit(cpu, cpu->c, 0);
    break;

  // sub d
  case 0x92:
    z80_sub_8bit(cpu, cpu->d, 0);
    break;

  // sub e
  case 0x93:
    z80_sub_8bit(cpu, cpu->e, 0);
    break;

  // sub h
  case 0x94:
    z80_sub_8bit(cpu, cpu->h, 0);
    break;

  // sub l
  case 0x95:
    z80_sub_8bit(cpu, cpu->l, 0);
    break;

  // sub (hl)
  case 0x96:
    z80_sub_8bit(cpu, z80_read(cpu, HL(cpu)), 0);
    break;

  // sub a
  case 0x97:
    z80_sub_8bit(cpu, cpu->a, 0);
    break;

  // sbc a,b
  case 0x98:
    z80_sbc_8bit(cpu, cpu->b);
    break;

  // sbc a,c
  case 0x99:
    z80_sbc_8bit(cpu, cpu->c);
    break;

  // sbc a,d
  case 0x9a:
    z80_sbc_8bit(cpu, cpu->d);
    break;

  // sbc a,e
  case 0x9b:
    z80_sbc_8bit(cpu, cpu->e);
    break;

  // sbc a,h
  case 0x9c:
    z80_sbc_8bit(cpu, cpu->h);
    break;

  // sbc a,l
  case 0x9d:
    z80_sbc_8bit(cpu, cpu->l);
    break;

  // sbc a,(hl)
  case 0x9e:
    z80_sbc_8bit(cpu, z80_read(cpu, HL(cpu)));
    break;

  // sbc a,a
  case 0x9f:
    z80_sbc_8bit(cpu, cpu->a);
    break;

  // xor a
  case 0xaf:
    z80_xor(cpu, cpu->a);
    break;

  // or b
  case 0xb0:
    z80_or(cpu, cpu->b);
    break;

  // or c
  case 0xb1:
    z80_or(cpu, cpu->c);
    break;

  // or a
  case 0xb7:
    z80_or(cpu, cpu->a);
    break;

  // cp b
  case 0xb8:
    z80_cp(cpu, cpu->b);
    break;

  // cp c
  case 0xb9:
    z80_cp(cpu, cpu->c);
    break;

  // cp h
  case 0xbc:
    z80_cp(cpu, cpu->h);
    break;

  // cp l
  case 0xbd:
    z80_cp(cpu, cpu->l);
    break;

  // cp (hl)
  case 0xbe:
    z80_cp(cpu, z80_read(cpu, HL(cpu)));
    break;

  // ret nz
  case 0xc0:
    z80_ret(cpu, !z80_getflag(cpu, F_Z));
    break;

  // pop bc
  case 0xc1: {
    word bc = z80_pop16(cpu);
    cpu->b = bc >> 7;
    cpu->c = bc & 0xff;
  } break;

  // jp nn
  case 0xc3:
    z80_jp(cpu, z80_fetch16(cpu), true);
    break;

  // push bc
  case 0xc5:
    z80_push16(cpu, BC(cpu));
    break;

  // add a,n
  case 0xc6:
    z80_add_8bit(cpu, z80_fetch(cpu), 0);
    break;

  // ret z
  case 0xc8:
    z80_ret(cpu, z80_getflag(cpu, F_Z));
    break;

  // ret
  case 0xc9:
    z80_ret(cpu, true);
    break;

  // jp z,nn
  case 0xca:
    z80_jp(cpu, z80_fetch16(cpu), z80_getflag(cpu, F_Z));
    break;

  // call nn
  case 0xcd: {
    word addr = z80_fetch16(cpu);
    z80_push16(cpu, cpu->pc);
    cpu->pc = addr;
  } break;

  // ret nc
  case 0xd0:
    z80_ret(cpu, !z80_getflag(cpu, F_C));
    break;

  // pop de
  case 0xd1: {
    word de = z80_pop16(cpu);
    cpu->d  = de >> 8;
    cpu->e  = de & 0xff;
  } break;

  // out (n),a
  case 0xd3:
    cpu->io_out(z80_pair(cpu->a, z80_fetch(cpu)), cpu->a);
    break;

  // push de
  case 0xd5:
    z80_push16(cpu, DE(cpu));
    break;

  // sub n
  case 0xd6:
    z80_sub_8bit(cpu, z80_fetch(cpu), 0);
    break;

  // exx
  case 0xd9: {
    SWAP(cpu->b, cpu->b_);
    SWAP(cpu->c, cpu->c_);
    SWAP(cpu->d, cpu->d_);
    SWAP(cpu->e, cpu->e_);
    SWAP(cpu->h, cpu->h_);
    SWAP(cpu->l, cpu->l_);
  } break;

  // in a,(n)
  case 0xdb:
    cpu->a = cpu->io_in(z80_pair(cpu->a, z80_fetch(cpu)));
    break;

  // pop hl
  case 0xe1: {
    word hl = z80_pop16(cpu);
    cpu->h  = hl >> 8;
    cpu->l  = hl & 0xff;
  } break;

  // push hl
  case 0xe5:
    z80_push16(cpu, HL(cpu));
    break;

  // and n
  case 0xe6:
    z80_and(cpu, z80_fetch(cpu));
    break;

  // jp (hl)
  case 0xe9:
    z80_jp(cpu, HL(cpu), true);
    break;

  // ex de,hl
  case 0xeb: {
    word orig_d = cpu->d;
    word orig_e = cpu->e;
    cpu->d = cpu->h;
    cpu->e = cpu->l;
    cpu->h = orig_d;
    cpu->l = orig_e;
  } break;

  // or n
  case 0xf6:
    z80_or(cpu, z80_fetch(cpu));
    break;

  // pop af
  case 0xf1:
    cpu->f = z80_pop(cpu);
    cpu->a = z80_pop(cpu);
    break;

  // push af
  case 0xf5:
    z80_push16(cpu, AF(cpu));
    break;

  // cp n
  case 0xfe:
    z80_cp(cpu, z80_fetch(cpu));
    break;

  default: {
    char err[128];
    snprintf(err, sizeof(err), "unsupported opcode: %02x", opcode);
    z80_error(err);
  }
  }

  return true;
}

bool z80_execute_misc(z80_cpu *cpu, byte opcode) {
  switch (opcode) {

  // sbc hl,bc
  case 0x42:
    z80_sbc_16bit(cpu, BC(cpu));
    break;

  // ld (nn),de
  case 0x53: {
    word addr = z80_fetch16(cpu);
    z80_write(cpu, addr,   cpu->e);
    z80_write(cpu, addr+1, cpu->d);
  } break;

  // sbc hl,de
  case 0x52:
    z80_sbc_16bit(cpu, DE(cpu));
    break;


  // ld de,(nn)
  case 0x5b: {
    word addr = z80_fetch16(cpu);
    cpu->d = z80_read(cpu, addr);
    cpu->e = z80_read(cpu, addr+1);
  } break;

  default: {
    char err[128];
    snprintf(err, sizeof(err), "unsupported opcode: ed %02x", opcode);
    z80_error(err);
  }
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
  } else if (instr->prefix_len == 1 && instr->prefix[0] == 0xed) {
    return z80_execute_misc(cpu, instr->opcode);
  } else {
    z80_error("unsupported prefixed instruction");
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
  printf("pc: %04x\n", cpu->pc);
  printf("sp: %04x\n", cpu->sp);
  printf("\n");
  z80_debug_print_flags(cpu);
  printf("\n");
  printf("a: %02x f: %02x, %5u\n", cpu->a, cpu->f, z80_pair(cpu->a, cpu->f));
  printf("b: %02x c: %02x, %5u\n", cpu->b, cpu->c, z80_pair(cpu->b, cpu->c));
  printf("d: %02x e: %02x, %5u\n", cpu->d, cpu->e, z80_pair(cpu->d, cpu->e));
  printf("h: %02x l: %02x, %5u\n", cpu->h, cpu->l, z80_pair(cpu->h, cpu->l));
}
