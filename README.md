#picocom
Minimal dumb-terminal emulator

by Nick Patavalis (npat@efault.net)

The latest release can be downloaded from:

> https://github.com/npat-efault/picocom/releases

As its name suggests, *picocom* is a minimal dumb-terminal emulation
program. It is, in principle, very much like minicom, only it's "pico"
instead of "mini"! 

It was designed to serve as a simple, manual, modem configuration,
testing, and debugging tool. It has also served (quite well) as a
low-tech serial communications program to allow access to all types of
devices that provide serial consoles. It could also prove useful in
many other similar tasks.

It is ideal for embedded systems since its memory footprint is minimal
(approximately 30K, when stripped). Apart from being a handy little
tool, *picocom's* source distribution includes a simple, easy to use,
and thoroughly documented terminal-management library, which could
serve other projects as well. This library hides the termios(3) calls,
and provides a less complex and safer (though certainly less
feature-rich) interface. *picocom* runs on Linux, and with no or minor
modifications it could run on any Unix-like system with the termios(3)
library.

For a description of picocom's operation, its command line options,
and usage examples, see the manual page included in the source
distribution as "picocom.8", and also html-ized as "picocom.8.html".

People who have contibuted to picocom, by offering feature
implementations, bug-fixes, corrections, and suggestions are listed in
the "CONTRIBUTORS" file.

Please feel free to send comments, requests for new features (no
promisses, though!), bug-fixes and rants, to the author's email
address shown at the top of this file.

## Compilation / Installation

Change into picocom's source directory and say:

```
  make
```

This will be enough to compile picocom for most modern Unix-like
systems. If you want, you can then strip the resulting binary like
this:

```
  strip picocom
```

Striping the binary is not required, it just reduces its size by a few
kilobytes. Then you can and copy the picocom binary, as well as the
man-page, to wherever you put your binaries and man-pages. For
example:

```
  cp picocom ~/bin
  cp picocom.8 ~/man/man8
```

Again, this is not strictly necessary. You can run picocom and read
its man-page directly from the source directory. 

If something goes wrong and picocom can't compile cleanly, or if it's
lacking a feature you need, take a look at the included Makefile. It's
very simple and easy to understand. It allows you to select
compile-time options and enable or disable some compile-time features
by commenting in or out the respective lines.

## Using picocom

If your computer is a PC and has the standard on-board RS-233 ports
(usually accessible as two male DB9 connectors at the back) then under
Linux these are accessed through device nodes most likely named:
`/dev/tty S0` and `/dev/ttyS1`. If your computer has no on-board
serial ports, then you will need a USB-to-Serial adapter (or something
similar). Once inserted to a USB port and recognized by Linux, a
device node is created for each serial port accessed through the
adapter(s). These nodes are most likely named `/dev/ttyUSB0`,
`/dev/ttyUSB1`, and so on. For other systems and Unix-like OSes you
will have to consult their documentation as to how the serial port
device nodes are named.  Lets assume your serial port is accessed
through a device node called `/dev/ttyS0`.

You can start picocom with its default option values (default serial
port settings) like this:

```
picocom /dev/ttyS0
```

If you have not installed the picocom binary to a suitable place, then
you can run it directly from the source distribution directory like
this:

```
./picocom /dev/ttyS0
```

If this fails with a message like:

```
FATAL: cannot open /dev/ttyS0: Permission denied
```

This means that you do not have permissions to access the serial
port's device node. To overcome this you can run picocom as root:

```
sudo picocom /dev/ttyS0
```

Alternatively, and preferably, you can add yourself to the user-group
that your system has for allowing access to serial ports. For most
Unix-like systems this group is called "dialout". Consult you system's
documentation to find out how you can do this (as it differs form
system to system). On most Linux systems you can do it like this:

```
sudo usermod -a -G dialout username
```

You can explicitly set one or more of the serial port settings to the
desired values using picocom's command line options. For example, to
set the baud-rate to 115200bps (the default is 9600bps), and enable
hardware flow-control (RTS/CTS handshake) you can say:

```
picocom --baud 115200 --flow h /dev/ttyS0
```

or:

```
picocom --baud 115200 --flow h /dev/ttyS0
```

To see all available options run picocom like this:

```
picocom --help
```

Once picocom starts, it initializes the serial port and prints the
message:

```
Terminal is ready
```

From now on, every character you type is sent to the serial port, and
every character received from the serial port is sent ro your
terminal.  Including control and special characters. Assuming that
there is nothing connected to the other end of your serial port, to
respond to the characters you send it (e.g. echo them back to you),
then nothing that you type in picocom will appear on your
terminal. This is normal.

To exit picocom you have to type:

```
C-a, C-x
``

Which means you have to type [Conttol-A] followed by [Control-C]. You
can do this by pressing and holding down the [Control] key, then
pressing (and releasing) the [A] key and then pressing (and releasing)
the [X] key (while you still keep [Control] held down).

This `C-a` is called the "escape character". It is used to inform
picocom that the next character typed is to be interpreted as a
command to picocom itself (in this case the exit command) and not to
be sent-down to the serial port. There are several other commands
(other than `C-a`, `C-x`), all prefixed by `C-a`.

Next you should take a look at the very detailed picocom manual
page. It can be accessed like this (assuming you are inside the
picocom distribution source directory):

```
man ./picocom.8
```

or (assuming you have installed the manual page to a suitable place):

```
man picocom
```

Thanks for using picocom
