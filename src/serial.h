#ifndef SERIAL_H
#define SERIAL_H

#include <stdlib.h>
#include <stdio.h>

#include "z80.h"

void serial_mount(byte port, const char *f);
byte serial_in(word port);
void serial_out(word port, byte val);

#endif // SERIAL_H
