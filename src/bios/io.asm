DELETE = 127

SERIAL = 0

parse  = $0102 ;   2b

; buffer a line of input, terminated by \0, not \n
; clobbers: a,hl
readl:
  ld   hl,inputl
.loop:
  in   a,(SERIAL)
  ; check if no key pressed...
  cp   0
  ; if so, check again
  jr   z,.loop
  cp   '\n'
  jr   z,.done
  cp   DELETE
  jr   z,.backspace
  cp   '\b'
  jr   z,.backspace
  cp   '\033'
  jr   z,.esc
  ; write the char to the end of the buffer
  ld   (hl),a
  ; move to the next spot in the buffer
  inc  hl
  ; echo the char
  out  (SERIAL),a
  ; repeat
  jr   .loop
.backspace:
  ; load the start of the bufferr
  ld   de,inputl
  ; check if we're already at it
  sbc  hl,de
  ; if so, ignore backspace
  jr   z,.loop
  ; move cursor back,
  ld   a,'\b'
  out  (SERIAL),a
  ; replace prev char with space (moves forward),
  ld   a,' '
  out  (SERIAL),a
  ; and go back again
  ld   a,'\b'
  out  (SERIAL),a
  dec  hl
  jr   .loop
.esc:
  ld   hl,monitor_start
  call print
  jp   monitor
.done:
  out  (SERIAL),a
  ; mark eol with a null char
  ld   (hl),'\0'
  ; start reads at start of buffer
  ld   hl,inputl
  ld   (parse),hl
  ret

; output the hex byte in a
; clobbers: a,b
hex_out:
  ; save full byte into a
  ld   b,a
  ; extract high nibble
  rrca
  rrca
  rrca
  rrca
  and  $0f
  ; print it
  call .nibble
  ; extract low nibble
  ld   a,b
  and  $0f
  ; print it
  call .nibble
  ret
.nibble:
  ; if nibble < 10, print it's digit
  cp   10
  jr   c,.digit
  ; otherwise, print it's letter
  add  a,'A'-10
  out  (SERIAL),a
  ret
.digit:
  add  a,'0'
  out  (SERIAL),a
  ret

; print the hex word in hl
; clobbers a,b,hl
hex_word_out:
  ld   a,h
  call hex_out
  ld   a,l
  call hex_out
  ret

; read a single char from the input buffer and advance to the next
; char (skips whitespace) (returns in a)
; clobbers: a,hl
char_from_inputl:
  ld   hl,(parse)
  ld   a,(hl)
  inc  hl
  ld   (parse),hl
  cp   ' '
  jr   z,char_from_inputl
  ret

; undo previous call to char_from_inputl
; clobbers: a,hl
unget_char:
  ld   hl,(parse)
  dec  hl
  ld   a,(hl)
  cp   ' '
  jr   z,unget_char
  ld   (parse),hl
  ret

; read in a hex byte from input buffer (returns in a) (produces
; garbage for invalid hex)
; clobbers: a,b,hl
hex_in:
  ; read a single hex char
  call .nibble
  ; move it to high nibble spot
  rlca
  rlca
  rlca
  rlca
  ; save it for now
  ld   b,a
  ; read another char
  call .nibble
  ; it is in the low nibble spot. now or back in the high nibble we
  ; just saved
  or   b
  ret
.nibble:
  call char_from_inputl
  ; if less than 'A' in ascii code, must be a digit
  cp   'A'
  jr   c,.digit
  ; if less than 'a' in ascii code, must be an uppercase char
  cp   'a'
  jr   c,.uppercase
  ; otherwise, it must be lowercase
  sub  'a'-10
  ret 
.uppercase:
  sub  'A'-10
  ret
.digit:
  sub  '0'
  ret

; read a 16-byte hex value into hl
; clobbers: a,b,hl
hex_word_in:
  call hex_in
  ld   e,a
  call hex_in
  ld   h,e
  ld   l,a
  ret

; print null-terminated string (hl)
print:
  ld   a,(hl)
  cp   '\0'
  ret  z
  out  (SERIAL),a
  inc  hl
  jr   print

