Using RTS/DTR lines to control microcontroller reset signals
============================================================

Introduction
------------

In bare-metal software development, it's common practice to "misuse"
the UART handshake lines to control the reset signals of the target
microcontroller.  For example, we often use RTS to drive the
microcontroller's reset input pin and DTR to switch between bootloader
and normal operation mode.

If the microcontroller's reset input uses negative logic (low = reset,
high = running), just connect the RTS TTL-level output (which is
itself inverted) to the microcontroller's reset input pin.

Problem at open
---------------

In both Linux and macOS, we observe that the RTS handshake line will be
driven high (TTL-level output low) when `open()` is called, even when
the port is used without hardware handshake.

This behavior cannot be avoided without patching the linux kernel
driver (see also http://stackoverflow.com/a/21753723/2880699).

Note, that in FreeBSD the RTS handshake line is not been changed upon
`open()` but staying left in the state whatever it was before. Tested
with FreeBSD 11, both Ftdi and Prolific adapters, picocom 3.1.

Work-around using the user-space API
------------------------------------

All we can do in our terminal software, is to reset the RTS signal
back to low, immediately after the `open()` call.  But even if we do
this quite fast, directly after `open()`, the RTS signal will still
transition to high for a short time. This may reset the
microcontroller.

Using the picocom `--lower-rts` command line option, I measured about
50µs-70µs on my Linux machine and 250µs-450µs on my old Macbook Pro
running macOS (both tested with an FTDI FT2232H USB-to-Serial adapter).
But note that there is no hard guarantee for these times; the OS may
preempt picocom between the `open()` and the "lower-rts" calls.

If the possibility of a microcontroller-reset is not acceptable, you
could guard against it by adding some hardware between the RTS line
and the microcontroller's reset input pin. Most likely, a simple low
pass RC filter will do the job.

Using python3
-------------

Python's serial communication example tool `python3 -m
serial.tools.miniterm` provides a command line option `--rts 0` which
also lowers the RTS line after opening the port.  On the same linux
machine mentioned above, I measured an RTS transition duration of
about 150µs-170µs.

When using something like `python3 -c "import serial; ser =
serial.Serial('/dev/ttyUSB0'); ser.setRTS(False); ... "`, I measured a
duration of about 180µs-260µs.

Deeper analysis
---------------

Analyzed using:

- Macbook Pro
- Ft2232H based Usb serial adapter
- Kubuntu 16.04 64 bit
- picocom 2.3a, with `--lower-rts` command line option

CRTSCTS=0x80000000. This bit is set in tty->termios->c_cflag when the
uart is used with Rts/Cts handshake.

When calling open() / ftdi_open(), I observed that this flag seems to
be undefined. I found it either set or unset, depending on if the last
session was with or without hardware handshake.

So user space API open() calls the following functions of the
`ftdi_sio` kernel driver module:

    ftdi_open(c_cflag=000008BD)           // CRTSCTS is undefined here
      ftdi_set_termios(c_cflag=000008BD)  // called by ftdi_open
    ftdi_dtr_rts(on=1, c_cflag=000008BD)  // regardless if using hw-flow
      update_mctrl(set=0006, clear=0000)  // CRTSCTS still undefined here

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

```
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
```
