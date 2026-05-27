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

addr   = $0100 ;   2b
inputl = $0000 ; 256b

monitor:
  ; start at address $1000
  ld   hl,$1000
  ld   (addr),hl
.loop:
  call monitor_prompt
  call readl
  call execl
  jr   .loop

; print the prompt, including the address
monitor_prompt:
  ld   hl,pre_prompt
  call print
  ld   hl,(addr)
  call hex_word_out
  ld   hl,post_prompt
  jp   print

; run a full command from the input buffer
execl:
; some commands allow more commands directly after them. they jump
; back here
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
  ; if not a command char, undo that read and read as hex.
  call unget_char
  call hex_in
  ld   d,a
  call char_from_inputl
  ; if comma or eol, write the byte to addr
  cp   ','
  jp   z,.write
  ld   b,a
  ; we have to preserve the null char for the subsequent execl jump.
  call unget_char
  ld   a,b
  cp   '\0'
  jp   z,.write
  ; if not, read another hex byte and treat it as a full address to
  ; move to
  call hex_in
  ld   e,a
  ld   (addr),de
  jr   execl
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
  ; when the user code returns, it will return on behalf of the execl
  ; function (basically tail call optimisation)
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
  ; (we have to preserve hl as it contains where we have already
  ;  printed to)
  push hl
  or   a
  sbc  hl,de
  pop  hl
  ; if we haven't, keep printing until we have
  jr   c,.range_echo_loop
  jr   z,.range_echo_loop
  ret
.write:
  ld   hl,(addr)
  ld   (hl),d
  inc  hl
  ld   (addr),hl
  jp   execl
  ret

; print a line of an almost-canonical hexdump starting at hl|$fff0
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
  ; if the char is not printable, show a dot instead
  jr   nc,.show_dot
  ; otherwise, we are safe to print it
  out  (SERIAL),a
  jr   .ascii_next
.show_dot:
  push hl
  ld   hl,non_printable_char
  call print
  pop  hl
.ascii_next:
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
  .asciiz "\033[35m"
post_prompt:
  .asciiz "\033[90m ~> \033[0m"
