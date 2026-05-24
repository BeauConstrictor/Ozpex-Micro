#ifndef RAM_H
#define RAM_H

#include <stdlib.h>
#include <stdio.h>

#include "z80.h"

void ram_create(z80_device *ram);
ssize_t ram_load_image(z80_device *ram, FILE *f);

#endif // RAM_H
