SERIAL = 0

; start:
;   ld   bc,message
;   call print
;   halt

print:
  ld   bc,message
loop:
  ld   a,(bc)
  or   a
  jr   z,done
  out  (SERIAL),a
  inc  bc
  jr   loop
done:
;  ret
  halt

message:
  .asciiz "Hello, world!\n"
