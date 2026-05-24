SERIAL = 0

start:
  ld   bc,message
loop:
  ld   a,(bc)
  or   a
  jr   z,done
  out  (SERIAL),a
  inc  bc
  jr   loop
done:
  halt

message:
  .asciiz "Hello, world!\n"
