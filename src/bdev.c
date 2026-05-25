#include "z80.h"

#include "global.h"
#include "bdev.h"

typedef enum {
  bdev_t_sectd,
  bdev_t_xmem
} bdev_type;

typedef struct {
  byte buffer[256];
  FILE *image;
} bdev_sectd;

typedef struct {
  byte memory[65536];
  byte sector;
} bdev_xmem;

typedef struct {
  bdev_type kind;
  union {
    bdev_sectd ss;
    bdev_xmem  xm;
  };
} bdev_dev;

typedef struct {
  bdev_dev *devices[256];
  byte selected;
  byte sector;
  word block_s; // address of the start of the block
  word sect_s;  // address of the sector byte
  word dev_s;   // address of the device byte
} bdev_devs;

byte bdev_sectd_read(bdev_sectd *dev, byte addr) {
  return dev->buffer[addr];
}

void bdev_sectd_write(bdev_sectd *dev, byte addr, byte val) {
  dev->buffer[addr] = val;
}

void bdev_sectd_setsect(bdev_sectd *dev, byte old, byte new) {
  // flush buffer to file[old*256] -> file[old*256+255];
  // write into buffer file[new];
}

byte bdev_xmem_read(bdev_xmem *dev, byte addr) {
  return dev->memory[(dev->sector << 8) | addr];
}

void bdev_xmem_write(bdev_xmem *dev, byte addr, byte val) {
  dev->memory[(dev->sector << 8) | addr] = val;
}

void bdev_xmem_setsect(bdev_xmem *dev, byte old, byte new) {
  dev->sector = new;
}

void bdev_setsect(bdev_devs *devs, bdev_dev *dev, byte new) {
  switch (dev->kind) {
  case bdev_t_sectd:
    bdev_sectd_setsect(&dev->ss, devs->sector, new);
    break;
  case bdev_t_xmem:
    bdev_xmem_setsect(&dev->xm, devs->sector, new);
    break;
  }
  devs->sector = new;
}

byte bdev_read(void *state, word addr) {
  bdev_devs *devs = (bdev_devs*)state;
  bdev_dev *dev = devs->devices[devs->selected];

  if (addr == devs->sect_s) {
    return devs->sector;
  }

  if (addr == devs->dev_s) {
    return devs->selected;
  }

  if (dev == NULL) {
    return;
  }

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_sectd:
    return bdev_sectd_read(&dev->ss, block_addr);
  case bdev_t_xmem:
    return bdev_xmem_read(&dev->xm, block_addr);
  }
}

void bdev_write(void *state, word addr, byte val) {
  bdev_devs *devs = (bdev_devs*)state;
  bdev_dev *dev = devs->devices[devs->selected];

  if (addr == devs->dev_s) {
    bdev_setsect(devs, dev, devs->sector); // flush sector
    devs->selected = val;
    bdev_setsect(devs, dev, devs->sector); // load sector on new device
    return;
  }

  if (dev == NULL) {
    return;
  }

  if (addr == devs->sect_s) {
    bdev_setsect(devs, dev, val);
    return;
  }

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_sectd:
    bdev_sectd_write(&dev->ss, block_addr, val);
    break;
  case bdev_t_xmem:
    bdev_xmem_write(&dev->xm, block_addr, val);
    break;
  }
}

void bdev_create(z80_device *device, word block, word sect, word dev) {
  device->state   = malloc(sizeof(bdev_devs));
  device->read    = bdev_read;
  device->write   = bdev_write;
  device->state->block_s = block;
  device->state->sect_s  = sect;
  device->state->dev_s   = dev;
}
