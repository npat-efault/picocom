Using uart handshake lines to control microcontroller reset signals in bare metal development
=============================================================================================

Introduction
------------
In bare metal software development, it's common practice to misuse uart handshake lines for e.g. controlling the reset signal of the target microcontroller.
We're often using e.g. RTS to drive the microcontrollers reset input pin and e.g. DTR to switch between bootloader and normal operation mode.

For example, if the microcontrollers reset input uses negative logic (low = reset, high = running), just connect the RTS ttl level to the microcontrollers reset input pin.

Problem at open("port")
-----------------------
In both linux and osx, we observe, that the RTS handshake line will be driven active (positive voltage on RS232 signal level = logic low on ttl level implicitely within the `open()` call,
even when the uart port is used without hardware handshake.

This behaviour could not be avoided until patching the linux kernel driver (see also http://stackoverflow.com/a/21753723/2880699).

Working around from user space Api
----------------------------------

All we can do in our terminal software is, to reset the RTS signal back to ttl high level after the `open()` call.
But even when we do this quiet fast directly after the `open()` call, the RTS signal changes for a short time which might be enough to perform a reset of the microcontroller.

With my picocom patch (`--resetrtsafteropen` command line option), I'd measured about 50µs-70µs on my linux machine and 250µs-450µs on my old Macbook Pro with running OSX (tested with Ftdi FT2232H).
But note, that there is no guarantee for this times; because our PC is no realtime system, the OS might preempt our terminal application between toe `open()` and `ioctl()` calls. 

If this possibly microcontroller reset is disliked, we should work around against it by adding a bit more hardware between RTS and the microcontrollers reset input pin.
Sometimes a simple low pass filter (resistor and capacitor) do this job.

Deeper analysis
---------------
Analyzed using:
- Macbook Pro
- Ft2232H based Usb serial adapter
- Kubuntu 16.04 64 bit
- picocom 2.3a, patched with `--resetrtsafteropen` command line option

CRTSCTS=0x80000000. This bit is set in tty->termios->c_cflag when the uart is used with Rts/Cts handshake.
When calling open() / ftdi_open(), I'd observed that this flag seems to be undefined. I found it either set or unset, depending on if the last session was with or without hardware handshake.
So user space api open() calls the following functions of the `ftdi_sio` kernel driver module:
    ftdi_open(c_cflag=000008BD)                    // note that CRTSCTS flag is undefined here
        ftdi_set_termios(c_cflag=000008BD)         // called implicitely by ftdi_open
    ftdi_dtr_rts(on=1, c_cflag=000008BD)           // called regardless if using hardware handshake or not
        update_mctrl(set=0006, clear=0000)         // CRTSCTS flag is still undefined here

Hacking the ftdi_sio kernel driver module
-----------------------------------------

see also http://stackoverflow.com/a/40811405/2880699

    static void ftdi_dtr_rts(struct usb_serial_port *port, int on) {
        ...
        /* drop RTS and DTR */
        if (on)
            set_mctrl(port, TIOCM_DTR /*| TIOCM_RTS*/);    // <<-- HERE
        else
            clear_mctrl(port, TIOCM_DTR /*| TIOCM_RTS*/);  // <<-- and maybe even HERE
    }

Steps to perform on e.g. Kubuntu 16.04:
$ sudo apt-get install build-essential    ;# etc.
$ apt-get source linux-image-$(uname -r)  ;# of course, neets to have deb-src in /etc/apt/sources.list activated
-> this creates a ~/linux-4.4.0 with about 760 MiB source code including linux-4.4.0/drivers/usb/serial/ftdi_sio.c
$ cd ~/linux-4.4.0
$ chmod +x debian/scripts/misc/splitconfig.pl
$ chmod +x debian/scripts/config-check
$ debian/rules genconfigs
$ cp CONFIGS/amd64-config.flavour.generic .config
and then after each change in drivers/usb/serial/ftdi_sio.c:
$ make -C /lib/modules/$(uname -r)/build M=${PWD} drivers/usb/serial/ftdi_sio.ko
$ sudo rmmod ftdi_sio.ko
$ sudo insmod drivers/usb/serial/ftdi_sio.ko
ergo:
$ make -C /lib/modules/$(uname -r)/build M=${PWD} drivers/usb/serial/ftdi_sio.ko && sudo rmmod ftdi_sio.ko && sudo insmod drivers/usb/serial/ftdi_sio.ko
