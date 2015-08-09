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
low-tech "terminal-window" to allow operator intervention in PPP
connection scripts (something like the ms-windows "open terminal
window before / after dialing" feature). It could also prove useful in
many other similar tasks. 

It is ideal for embedded systems since its memory footprint is minimal
(approximately 30K, when stripped). Apart from being a handy little
tool, *picocom* source distribution includes a simple, easy to use,
and thoroughly documented terminal-management library, which could
serve other projects as well. This library hides the termios(3) calls,
and provides a less complex and safer (though certainly less
feature-rich) interface. *picocom* runs on Linux, and with no or minor
modifications it could run on any Unix system with the termios(3)
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

This will be enough to compile picocom for most Unix-like systems. If
you want, you can then strip the resulting binary like this:

```
  strip picocom
```

and copy it, as well as the man-page, to wherever you put your
binaries and man-pages. For example:

```
  cp picocom ~/bin
  cp picocom.8 ~/man/man8
```

If something goes wrong and picocom can't compile cleanly, or if it's
lacking a feature you need, take a look at the included Makefile. It's
very simple and easy to understand. It allows you to select
compile-time options and enable or disable some compile-time features
by commenting in or out the respective lines.
