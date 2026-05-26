#ifndef BDEV_H
#define BDEV_H

#include "z80.h"

typedef struct bdev_dev bdev_dev;
typedef struct bdev_devs bdev_devs;

bdev_devs *bdev_create(z80_device *device, word block, word sect,
        word dev, word status);

void bdev_install(bdev_devs *devs, byte slot, bdev_dev *dev);

bdev_dev *bdev_create_sectd(bool bootable, FILE *f);
bdev_dev *bdev_create_xmem();

#endif // BDEV_H
