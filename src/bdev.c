#include "z80.h"

#include "global.h"
#include "bdev.h"

typedef enum {
  bdev_t_sectd
} bdev_type;

typedef struct {
  byte buffer[256];
} bdev_sectd;

typedef struct {
  bdev_type kind;
  union {
    bdev_sectd ss;
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

void bdev_setsect(bdev_devs *devs, bdev_dev *dev, byte new) {
  switch (dev->kind) {
  case bdev_t_sectd:
    bdev_sectd_setsect(dev, devs->sector, new);
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

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_sectd:
    return bdev_sectd_read(&dev->ss, block_addr);
  }
}

void bdev_write(void *state, word addr, byte val) {
  bdev_devs *devs = (bdev_devs*)state;
  bdev_dev *dev = devs->devices[devs->selected];

  if (addr == devs->sect_s) {
    bdev_setsect(devs, dev, val);
    return;
  }

  if (addr == devs->dev_s) {
    bdev_setsect(devs, dev, devs->sector); // flush sector
    devs->selected = val;
    return;
  }

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_sectd:
    bdev_sectd_write(&dev->ss, block_addr, val);
    break;
  }
}

void bdev_create(z80_device *device, word block, word sect, word dev) {
  device->state   = malloc(sizeof(bdev_devs));
  device->read    = bdev_read;
  device->write   = bdev_write;
  device->block_s = block;
  device->sect_s  = sect;
  device->dev_s   = dev;
}
