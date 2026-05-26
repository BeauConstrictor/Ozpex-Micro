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
; !       - jump to the current address as a subroutine
; .[nnnn] - print the value in the current address. pass a second
;           address to print everything in the range.
; +       - increment the address
; -       - decrement the address
;
; To write a byte to your address, simply enter the byte in hex. To
; write more bytes into the following addresses, use a comma:
;
;   1000 01,02,03
;
; This command will write a $01 into $1000, a $02 in $1001 and a $03
; into $1002.
;
; KNOWN BUGS:
; - You can overflow the input buffer, which will overwwrite your
;   address.
; - Invalid inputs produce garbage output.

DELETE = 127

SERIAL = 0

inputl = $0000 ; 256b
addr   = $0100 ;   2b
parse  = $0102 ;   2b

  .org $e000

start:
  ; initialise the stack to the top of ram
  ld   sp,$bfff
  ; start at address $1000
  ld   hl,$1000
  ld   (addr),hl
mainloop:
  call prompt
  call readl
  call execl
  jr   mainloop
  halt

; buffer a line of input, terminated by \0, not \n
readl:
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
  ld   de,inputl
  sbc  hl,de
  jr   z,.loop
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

; print the prompt, including the address
prompt:
  ld   hl,pre_prompt
  call print
  ld   hl,(addr)
  call hex_word_out
  ld   hl,post_prompt
  jp   print

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
  cp   '+'
  jr   z,.next
  cp   '-'
  jr   z,.prev
  cp   '!'
  jr   z,.jump
  cp   '.'
  jr   z,.echo
  call unget_char
  call hex_in
  ld   d,a
  call char_from_inputl
  cp   ','
  jp   z,.write
  ld   b,a
  call unget_char
  ld   a,b
  cp   '\0'
  jp   z,.write
  call hex_in
  ld   e,a
  ld   (addr),de
  jr   .dispatch
  ret
.next:
  ld   hl,(addr)
  inc  hl
  ld   (addr),hl
  ret
.prev:
  ld   hl,(addr)
  dec hl
  ld   (addr),hl
  ret
.jump:
  ld   hl,(addr)
  jp   hl
.echo:
  call char_from_inputl
  cp   '\0'
  jr   nz,.range_echo
  ; print the value as a single hex byte
  ld   hl,type_u8
  call print
  ld   hl,(addr)
  ld   a,(hl)
  call hex_out
  ld   a,'\n'
  out  (SERIAL),a
  ; print the value as 2 hex bytes (little-endian)
  ld   hl,type_u16
  call print
  ld   hl,(addr)
  inc  hl
  ld   a,(hl)
  call hex_out
  dec  hl
  ld   a,(hl)
  call hex_out
  ld   a,'\n'
  out  (SERIAL),a
  ; print the value as an ascii char
  ld   hl,type_ascii
  call print
  ld   hl,(addr)
  ld   a,(hl)
  call is_printable
  jr   nc,.echo_nonprintable
  out  (SERIAL),a
  ld   a,'\n'
  out  (SERIAL),a
  ret
.echo_nonprintable:
  ld   hl,non_printable_char
  call print
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
  call print_hexdump_line
  ; check if we have gone past our final address
  push hl
  or   a
  sbc  hl,de
  pop  hl
  jr   c,.range_echo_loop
  jr   z,.range_echo_loop
  ret
.write:
  ld   hl,(addr)
  ld   (hl),d
  inc  hl
  ld   (addr),hl
  jp   .dispatch
  ret

; print a line of a canonical hexdump starting at hl
print_hexdump_line:
  push hl
  ld   hl,hexdump_addr
  call print
  pop  hl
  ; make sure to print from start of line (xxx0)
  ld   a,l
  and  $f0
  ld   l,a
  push hl
  call hex_word_out
  push hl
  ld   hl,hexdump_addrend
  call print
  pop  hl
.hex_loop:
  ld   a,(hl)
  call hex_out
  ld   a,' '
  out  (SERIAL),a
  inc  hl
  ; check if at start of next line
  ld   a,l
  and  $0f
  ; if not, print next byte
  jr   nz,.hex_loop
  ld   a,' '
  out  (SERIAL),a
  ; go back to start of original line
  pop hl
.ascii_loop:
  ld   a,(hl)
  call is_printable
  jr   nc,.show_dot
  jr   .ascii_next
.show_dot:
  push hl
  ld   hl,non_printable_char
  call print
  pop  hl
.ascii_next:
  out  (SERIAL),a
  inc  hl
  ld   a,l
  and  $0f
  jr   nz,.ascii_loop
  ld   a,'\n'
  out  (SERIAL),a
  ret

; return if a is printable in the carry flag
is_printable:
    cp  $20
    jr  c,.no
    cp  $7f
    jr  nc,.no
    scf
    ret
.no:
    or  a
    ret

; print null-terminated string (hl)
print:
  ld   a,(hl)
  cp   '\0'
  ret  z
  out  (SERIAL),a
  inc  hl
  jr   print

type_u8:
  .asciiz "\033[90m   u8: \033[0m0x"
type_u16:
  .asciiz "\033[90m  u16: \033[0m0x"
type_ascii:
  .asciiz "\033[90mascii: \033[0m"

hexdump_addr:
  .asciiz "\033[34m"
hexdump_addrend:
  .ascii  "\033[0m  "
non_printable_char:
  .asciiz "\033[90m.\033[0m"

pre_prompt:
  .asciiz "\033[34m"
post_prompt:
  .asciiz "\033[32m ~> \033[0m"
