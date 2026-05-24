
#ifndef ROM_H
#define ROM_H

#include "z80.h"

void rom_create(z80_device *device, const char *path, size_t start,
        size_t size);

#endif // ROM_H
