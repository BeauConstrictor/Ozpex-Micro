DEV_READ   = $c000
DEV_SELECT = $c100
DEV_SECTOR = $c101
DEV_STATUS = $c102

BOOTLOADER = $0000

DEV_SECTD = $01 << 4
DEV_XMEM  = $02 << 4

BUSY      = %00000010
DEV_ID    = %11110000

  .org $e000

start:
  ; initialise the stack to the top of ram
  ld   sp,$bfff

bootmenu:
  ld   hl,bootmenu_msg
  call print
  call list_devices
.ask_device:
  ld   hl,bootmenu_prompt
  call print
  call readl
  ld   hl,ansi_reset
  call print
  ld   a,(inputl)
  cp   '\0'
  jr   z,.ask_device
  call hex_in
  call load_bootsect
  jp   BOOTLOADER

; wait until the busy flag is 0
; clobbers: a
busy_wait:
  ld   a,(DEV_STATUS)
  and  BUSY
  jr   nz,busy_wait
  ret

; load sector 0 of the disk in the a register into BOOTLOADER
load_bootsect:
  ; move to sector 0 of the chosen device
  ld   (DEV_SELECT),a
  call busy_wait
  ld   a,0
  ld   (DEV_SECTOR),a
  call busy_wait
  ; get pointers
  ld   de,BOOTLOADER
  ld   hl,DEV_READ
  ; copy exactly 256 bytes
  ld   b,0
.loop:
  ; copy from disk to ram
  ld   a,(hl)
  ld   (de),a
  ; move to next byte
  inc  hl
  inc  de
  ; continue until 256 bytes copied
  djnz .loop
  ; then return
  ret

list_devices:
  ld   b,0 ; device slot number
  ld   e,0 ; number of devices found
.loop:
  ld   a,b
  ld   (DEV_SELECT),a
  ld   a,(DEV_STATUS)
  and  $f0
  cp   0
  jr   z,.no_device
  inc  e
  ld   hl,number_style
  call print
  ld   a,b
  call hex_out
  ld   a,(DEV_STATUS)
  and  $f0
  cp   DEV_SECTD
  jr   z,.sectd
  cp   DEV_XMEM
  jr   z,.xmem
  jr   .unknown
.sectd:
  ld   hl,sectd_name
  call print
  jr   .check_bootable
.xmem:
  ld   hl,xmem_name
  call print
  jr   .check_bootable
.unknown:
  ld   hl,unknown_name
.check_bootable:
  ld   a,(DEV_STATUS)
  and  1 << 2
  cp   0
  jr   z,.not_bootable
  ld   hl,bootable_msg
  call print
.not_bootable:
  ld   a,'\n'
.no_device:
  inc  b
  out  (SERIAL),a
  jr   nz,.loop
  ld   a,'\n'
  out  (SERIAL),a
  ld   a,e
  cp   0
  ret  nz
  ld   hl,bootmenu_empty
  call print
  jp   monitor

  .include "io.asm"
  .include "monitor.asm"

bootmenu_msg:
  .text   "\033[35mOzpex Micro BIOS v0.1.0\033[0m\n"
  .text   "\033[90mPress <ESC> to enter the debug monitor.\033[0m\n\n" 
  .asciiz "Select a device to boot to:\n\n"
bootmenu_empty:
  .asciiz "\033[3A\033[2KNo devices installed, booting debug monitor instead...\n\n"
bootmenu_prompt:
  .asciiz "~> \033[35m"
ansi_reset:
  .asciiz "\033[0m"
number_style:
  .asciiz "\033[90m"
sectd_name:
  .asciiz "\033[90m: \033[32mSectored Storage\033[0m"
xmem_name:
  .asciiz "\033[90m: \033[32mExtended Memory\033[0m"
unknown_name:
  .asciiz ": Unknown Device"
bootable_msg:
  .asciiz " \033[33m(Bootable)\033[0m"
monitor_start:
  .asciiz "\033[90m<ESC>\n\n"

