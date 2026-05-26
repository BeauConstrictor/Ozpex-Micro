#include <string.h>
#include <stdio.h>

#include "z80.h"

#include "global.h"
#include "bdev.h"

typedef enum {
  bdev_t_nodev = 0,
  bdev_t_sectd = 1,
  bdev_t_xmem  = 2
} bdev_type;

typedef struct {
  byte buffer[256];
  FILE *image;
} bdev_sectd;

typedef struct {
  byte memory[65536];
  byte sector;
} bdev_xmem;

struct bdev_dev {
  bdev_type kind;
  bool bootable;
  union {
    bdev_sectd ss;
    bdev_xmem  xm;
  };
};

struct bdev_devs {
  bdev_dev *devices[256];
  byte selected;
  byte sector;
  word status_s; // address of status byte
  word block_s;  // address of the start of the block
  word sect_s;   // address of the sector byte
  word dev_s;    // address of the device byte
};

static void bdev_error(const char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

static byte bdev_sectd_read(bdev_sectd *dev, byte addr) {
  return dev->buffer[addr];
}

static void bdev_sectd_write(bdev_sectd *dev, byte addr, byte val) {
  dev->buffer[addr] = val;
}

static void bdev_sectd_flush(bdev_sectd *dev, byte sector) {
  (void)dev;
  (void)sector;

  // seek file to file[sector*256]
  // write buffer into it
}

static void bdev_sectd_setsect(bdev_sectd *dev, byte old, byte new) {
  (void)dev;
  (void)old;
  (void)new;
  // write into buffer file[new];
}

static byte bdev_xmem_read(bdev_xmem *dev, byte addr) {
  return dev->memory[(dev->sector << 8) | addr];
}

static void bdev_xmem_write(bdev_xmem *dev, byte addr, byte val) {
  dev->memory[(dev->sector << 8) | addr] = val;
}

static void bdev_xmem_setsect(bdev_xmem *dev, byte old, byte new) {
  (void)old;
  dev->sector = new;
}

static void bdev_flush(bdev_dev *dev, byte sector) {
  switch (dev->kind) {
  case bdev_t_nodev:
    bdev_error("attempt to manipulate a slot with no connected device");
    break;
  case bdev_t_sectd:
    bdev_sectd_flush(&dev->ss, sector);
    break;
  case bdev_t_xmem:
    break; // you don't need to flush extended memory
  }
}

static void bdev_setsect(bdev_devs *devs, bdev_dev *dev, byte new) {
  bdev_flush(dev, devs->sector);

  switch (dev->kind) {
  case bdev_t_nodev:
    bdev_error("attempt to manipulate a slot with no connected device");
    break;
  case bdev_t_sectd:
    bdev_sectd_setsect(&dev->ss, devs->sector, new);
    break;
  case bdev_t_xmem:
    bdev_xmem_setsect(&dev->xm, devs->sector, new);
    break;
  }
  devs->sector = new;
}

static byte bdev_status(bdev_devs *devs) {
  bdev_dev *dev = devs->devices[devs->selected];

  if (dev == NULL) {
    bdev_error("attempt to access empty device slot");
  }

  byte status = 0;

  status |= dev->kind << 4;
  status |= dev->bootable << 2;

  return status;
}

static byte bdev_read(void *state, word addr) {
  bdev_devs *devs = (bdev_devs*)state;
  bdev_dev *dev = devs->devices[devs->selected];

  if (addr == devs->status_s) {
    return bdev_status(devs);
  }

  if (addr == devs->sect_s) {
    return devs->sector;
  }

  if (addr == devs->dev_s) {
    return devs->selected;
  }

  if (dev == NULL) {
    bdev_error("ozm: attempted read to an empty device slot");
  }

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_nodev:
    bdev_error("attempt to manipulate a slot with no connected device");
    return 0x00;
  case bdev_t_sectd:
    return bdev_sectd_read(&dev->ss, block_addr);
  case bdev_t_xmem:
    return bdev_xmem_read(&dev->xm, block_addr);
  }
}

static void bdev_write(void *state, word addr, byte val) {
  bdev_devs *devs = (bdev_devs*)state;
  bdev_dev *dev = devs->devices[devs->selected];

  if (dev == NULL) {
    bdev_error("attempt to access empty device slot");
    return;
  }

  if (addr == devs->dev_s) {
    bdev_setsect(devs, dev, devs->sector); // flush sector
    devs->selected = val;
    dev = devs->devices[val];
    if (dev == NULL) {
      bdev_error("attempt to access empty device slot");
      return;
    }
    bdev_setsect(devs, dev, devs->sector); // load sector on new device
    return;
  }

  if (addr == devs->sect_s) {
    bdev_setsect(devs, dev, val);
    return;
  }

  byte block_addr = addr - devs->block_s;
  switch (dev->kind) {
  case bdev_t_nodev:
    bdev_error("attempt to manipulate a slot with no connected device");
    break;
  case bdev_t_sectd:
    bdev_sectd_write(&dev->ss, block_addr, val);
    break;
  case bdev_t_xmem:
    bdev_xmem_write(&dev->xm, block_addr, val);
    break;
  }
}

bdev_devs *bdev_create(z80_device *device, word block, word sect,
    word dev, word status) {
  device->state   = malloc(sizeof(bdev_devs));
  device->read    = bdev_read;
  device->write   = bdev_write;

  bdev_devs *d = device->state;
  memset(d, 0, sizeof(bdev_devs));
  d->status_s = status;
  d->block_s = block;
  d->sect_s  = sect;
  d->dev_s   = dev;

  return d;
}

void bdev_install(bdev_devs *devs, byte slot, bdev_dev *dev) {
  devs->devices[slot] = dev;
}

bdev_dev *bdev_create_sectd(bool bootable, FILE *f) {
  bdev_dev *dev = malloc(sizeof(bdev_dev));

  dev->kind = bdev_t_sectd;
  dev->bootable = bootable;
  dev->ss.image = f;

  fread(dev->ss.buffer, 1, sizeof(dev->ss.buffer), f);

  return dev;
}

bdev_dev *bdev_create_xmem() {
  bdev_dev *dev = malloc(sizeof(bdev_dev));

  dev->kind = bdev_t_xmem;
  dev->bootable = false;
  dev->xm.sector = 0;

  return dev;
}
