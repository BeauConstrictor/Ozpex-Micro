; Z80 hex monitor for the Ozpex Micro.
;
; The monitor keeps one piece of state: your current address.
; Some commands increment this address, some leave it untouched.
;
; To go to a new address, simply enter that address in hex and hit
; enter. You can also prefix a command with an address to do it all
; in one line.
;
; Commands are single characters followed by specific arguments (spaces
; are ignored):
;
; j       - jump to the current address as a subroutine
; =nn     - write a value to the current address
; /[nnnn] - print the value in the current address. pass a second
;           address to print everything in the range.
;
; KNOWN BUGS:
; - You can overflow the input buffer.
; - You can underflow the input buffer with backspace.
; - Trailing characters after a command are ignored.
; - If you print an address range and don't start on an xxx0 address,
;   it will look strange.
; - In places where hex is expected, you can type something else to
;   get broken results.

DELETE = 127

SERIAL = 0

inputl = $0000 ; 256b
addr   = $0100 ;   2b
parse  = $0102 ;   2b

  .org $e000

start:
  ld   sp,0bfffh
 
  ld   hl,$0000
  ld   (addr),hl

  ld   hl,message
  call print
mainloop:
  call readl
  call execl
  jr   mainloop
  halt

readl:
  call prompt
  ld   hl,inputl
.loop:
  in   a,(SERIAL)
  cp   0
  jr   z,.loop
  cp   '\n'
  jr   z,.done
  cp   DELETE
  jr   z,.backspace
  ld   (hl),a
  inc  hl
  out  (SERIAL),a
  jr   .loop
.backspace:
  ld   a,'\b'
  out  (SERIAL),a
  ld   a,' '
  out  (SERIAL),a
  ld   a,'\b'
  out  (SERIAL),a
  dec  hl
  jr   .loop
.done:
  out  (SERIAL),a
  ld   (hl),'\0'
  ret

prompt:
  ld   hl,(addr)
  call hex_word_out
  ld   hl,prompt_text
  call print
  ret

hex_out:
  ld   b,a
  rrca
  rrca
  rrca
  rrca
  and  $0f
  call .nibble
  ld   a,b
  and  $0f
  call .nibble
  ret
.nibble:
  cp   10
  jr   c,.digit
  add  a,'A'-10
  out  (SERIAL),a
  ret
.digit:
  add  a,'0'
  out  (SERIAL),a
  ret

hex_word_out:
  ld   a,h
  call hex_out
  ld   a,l
  call hex_out
  ret

char_from_inputl:
  ld   hl,(parse)
  ld   a,(hl)
  inc  hl
  ld   (parse),hl
  cp   ' '
  jr   z,char_from_inputl
  ret

unget_char:
  ld   hl,(parse)
  dec  hl
  ld   a,(hl)
  cp   ' '
  jr   z,unget_char
  ld   (parse),hl
  ret

hex_in:
  call .nibble
  rlca
  rlca
  rlca
  rlca
  ld   b,a
  call .nibble
  or   b
  ret
.nibble:
  call char_from_inputl
  cp   'A'
  jr   c,.digit
  cp   'a'
  jr   c,.uppercase
  sub  'a'-10
  ret 
.uppercase:
  sub  'A'-10
  ret
.digit:
  sub  '0'
  ret

hex_word_in:
  call hex_in
  ld   e,a
  call hex_in
  ld   h,e
  ld   l,a
  ret

execl:
  ld   hl,inputl
  ld   (parse),hl
.dispatch:
  call char_from_inputl
  cp   '\0'
  ret  z
  cp   'j'
  jr   z,.jump
  cp   '/'
  jr   z,.echo
  cp   '='
  jr   z,.write
  call unget_char
  call hex_word_in
  ld   (addr),hl
  jr   .dispatch
  ret
.jump:
  ld   hl,(addr)
  jp   hl
.echo:
  call char_from_inputl
  cp   '\0'
  jr   nz,.range_echo
  ld   hl,(addr)
  ld   a,(hl)
  call hex_out
  inc  hl
  ld   (addr),hl
  ld   a,'\n'
  out  (SERIAL),a
  ret
.range_echo:
  call unget_char
  call hex_word_in
  ld   d,h
  ld   e,l
  ld   hl,(addr)
.range_echo_loop:
  ld   a,l
  and  $0f
  jr   nz,.range_echo_not_newline
  ld   a,'\n'
  out  (SERIAL),a
  call hex_word_out
  ld   a,':'
  out  (SERIAL),a
  ld   a,' '
  out  (SERIAL),a
.range_echo_not_newline:
  ld   a,(hl)
  call hex_out
  ld   a,' '
  out  (SERIAL),a
  inc  hl
  ld   a,d
  cp   h
  jr   z,.range_echo_halfeq
  jr   .range_echo_loop
.range_echo_halfeq:
  ld   a,e
  cp   l
  jr   nz,.range_echo_loop
  call hex_out
  ld   a,'\n'
  out  (SERIAL),a
  ret
.write:
  call hex_in
  ld   hl,(addr)
  ld   (hl),a
  inc  hl
  ld   (addr),hl
  ret

print:
  ld   a,(hl)
  or   a
  ret  z
  out  (SERIAL),a
  inc  hl
  jr   print

message:
  .asciiz "OZM ROM Monitor v0.1.0\n\n"

prompt_text:
  .asciiz "$ "
