#include <unistd.h>
#include <stdio.h>

#include "global.h"
#include "z80.h"

#include "serial.h"

static void serial_err(const char *s) {
  fprintf(stderr, "ozm: %s\n", s);
  exit(1);
}

char getchar_nonblock(void) {
    char c;
    fd_set set;
    struct timeval timeout = {0, 0};

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    int ret = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);

    if (ret > 0) {
        if (read(STDIN_FILENO, &c, 1) == 1)
            return c;
    }

    return 0;
}

byte serial_in(word port) {
  port &= 0xff;
  if (port != 0) serial_err("currently, only port 0 is supported");
  return getchar_nonblock();
}

void serial_out(word port, byte val) {
  port &= 0xff;
  if (port != 0) serial_err("currently, only port 0 is supported");
  putchar(val);
  fflush(stdout);
}
