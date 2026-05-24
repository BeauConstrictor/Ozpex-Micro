SERIAL = 0

  .org $e000

start:
  ld   sp,0bfffh

  ld   hl,message
  call print
  halt

print:
  ld   a,(hl)
  or   a
  ret  z
  out  (SERIAL),a
  inc  hl
  jr   print

message:
  .asciiz "Hello, world!\n"
