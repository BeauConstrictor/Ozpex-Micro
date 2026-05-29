# Ozpex Micro

A fictional 8-bit retro microcomputer built for tkinkerers.

<img src="https://camo.githubusercontent.com/31851fc9832608d26da2ea969140dd35c9f0c29a4c802b279dd83b4db67d2801/68747470733a2f2f696d672e736869656c64732e696f2f6769746875622f6c6963656e73652f42656175436f6e7374726963746f722f6f7a7065782d36343f7374796c653d666c6174" alt="GNU GPL v2.0 License"/>

![A screenshot of a program typed into the hex
monitor](assets/screenshot.png)

The Ozpex Micro is another fictional retro computer in
my Ozpex family.  The Micro expands on the goals of the
[128](https://github.com/beauconstrictor/ozpex-128) through the
Z80 CPU, which is much easier to write software for thanks to it's
richer instruction set.

## Extensibility

Just like the 128, the Micro's design is rooted in extensibility -
the goal of being able to take identical software (including the OS
itself) that runs on one Ozpex Micro and run it directly on another
with a completely different hardware configuration, with zero hassle.

Every Micro starts off with what is called the 'standard environment',
basically a minimal hardware setup including 48K of RAM and a serial
port. Programs can then easily query for additional hardware and
either downgrade or fail altogether (if the program needs lots of
memory, for example) if certain expansions are not present.

These expansions are known as 'block devices' - individual 64k
address spaces, memory mapped through a single 256 byte page.
The device exposes a status byte which identifies the specific API
it supports (extended memory, disk, real time clock, etc.).

## The Emulator

For the Ozpex Micro, an emulator was written from the ground up in
C for excellent performance, as opposed to forking the 64's emulator
as the 128 did

### 1. Download & Build

```sh
git clone "https://github.com/beauconstrictor/ozpex-micro
cd ozpex-micro
make
```

This will download the emulator from this Github repo and build it.

### Using the CLI

 To start the emulator with a bootable disk in slot 1:

 ```
 ozm -m bdsk:<disk-image>@1
 ```

This will mount a device of type `bdsk`, a bootable disk. You can
also use `disk` if you don't want the BIOS to automatically boot to
your image. You pass device slots in 1/2-char hexadecimal.

These device types are available:

- `bdsk:<image>` - A sectored storage type device; marked as bootable.
- `disk:<image>` - A sectored storage type device.
- `xmem` - An extended memory type device (64K).
- `rtcm` - A real time clock module type device.

## License

This project is licensed under the
[GNU GPL v2](https://www.gnu.org/licenses/gpl-3.0.en.html).
