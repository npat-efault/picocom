# picocom
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

It is ideal for embedded systems since its memory footprint is small
(approximately 40K, when stripped and minimally configured). Apart
from being a handy little tool, *picocom's* source distribution
includes a simple, easy to use, and thoroughly documented
terminal-management library, which could serve other projects as
well. This library hides the termios(3) calls, and provides a less
complex and safer (though certainly less feature-rich) interface.

*picocom* runs and is, primarily, tested on Linux. With no, or with
minor, modifications it will run (and most of its features will work)
on any Unix-like system with a reasonably POSIX-compatible termios(3)
interface. Patches to support idiosyncrasies of specific Unix-like
operating systems are very welcome.

For a description of picocom's operation, its command line options,
and usage examples, see the manual page included in the source
distribution as "picocom.1", and also html-ized as "picocom.1.html".

People who have contributed to picocom, by offering feature
implementations, bug-fixes, corrections, and suggestions are listed in
the "CONTRIBUTORS" file.

Please feel free to send comments, requests for new features (no
promises, though!), bug-fixes and rants, to the author's email
address shown at the top of this file.

## Compilation / Installation

Change into picocom's source directory and say:

    make

This will be enough to compile picocom for most modern Unix-like
systems. If you want, you can then strip the resulting binary like
this:

    strip picocom

Striping the binary is not required, it just reduces its size by a few
kilobytes. Then you can copy the picocom binary, as well as the
man-page, to wherever you put your binaries and man-pages. For
example:


    cp picocom ~/bin
    cp picocom.1 ~/man/man1

Again, this is not strictly necessary. You can run picocom and read
its man-page directly from the source directory.

If something goes wrong and picocom can't compile cleanly, or if it's
lacking a feature you need, take a look at the included Makefile. It's
very simple and easy to understand. It allows you to select
compile-time options and enable or disable some compile-time features
by commenting in or out the respective lines. Once you edit the
Makefile, to recompile say:


    make clean
    make

If your system's default make(1) command is not GNU Make (or
compatible enough), find out how you can run GNU Make on your
system. For example:

    gmake clean
    gmake

Alternatively, you might have to make some trivial edits to the
Makefile for it to work with your system's make(1) command.

## Using picocom

If your computer is a PC and has the standard on-board RS-233 ports
(usually accessible as two male DB9 connectors at the back) then under
Linux these are accessed through device nodes most likely named:
`/dev/ttyS0` and `/dev/ttyS1`. If your computer has no on-board serial
ports, then you will need a USB-to-Serial adapter (or something
similar). Once inserted to a USB port and recognized by Linux, a
device node is created for each serial port accessed through the
adapter(s). These nodes are most likely named `/dev/ttyUSB0`,
`/dev/ttyUSB1`, and so on. For other systems and other Unix-like OSes
you will have to consult their documentation as to how the serial port
device nodes are named.  Lets assume your serial port is accessed
through a device node named `/dev/ttyS0`.

You can start picocom with its default option values (default serial
port settings) like this:

    picocom /dev/ttyS0

If you have not installed the picocom binary to a suitable place, then
you can run it directly from the source distribution directory like
this:

    ./picocom /dev/ttyS0

If this fails with a message like:

    FATAL: cannot open /dev/ttyS0: Permission denied

This means that you do not have permissions to access the serial
port's device node. To overcome this you can run picocom as root:

    sudo picocom /dev/ttyS0

Alternatively, and preferably, you can add yourself to the user-group
that your system has for allowing access to serial ports. For most
Unix-like systems this group is called "dialout". Consult you system's
documentation to find out how you can do this (as it differs form
system to system). On most Linux systems you can do it like this:

    sudo usermod -a -G dialout username

You will need to log-out and then log-in back again for this change to
take effect.

You can explicitly set one or more of the serial port settings to the
desired values using picocom's command line options. For example, to
set the baud-rate to 115200bps (the default is 9600bps), and enable
hardware flow-control (RTS/CTS handshake) you can say:

    picocom -b 115200 -f h /dev/ttyS0

or:

    picocom --baud 115200 --flow h /dev/ttyS0

To see all available options run picocom like this:

    picocom --help

Once picocom starts, it initializes the serial port and prints the
message:

    Terminal is ready

From now on, every character you type is sent to the serial port, and
every character received from the serial port is sent ro your
terminal.  Including control and special characters. Assuming that
there is nothing connected to the other end of your serial port, to
respond to the characters you send to it (e.g. echo them back to you),
then nothing that you type in picocom will appear on your
terminal. This is normal.

To exit picocom you have to type:

    C-a, C-x

Which means you have to type [Control-A] followed by [Control-X]. You
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

    man ./picocom.1

or (assuming you have installed the manual page to a suitable place):

    man picocom

Thanks for using picocom

## A low-tech terminal server

You can use *picocom* to patch-together a very simple, *very
low-tech*, terminal server.

The situation is like this: You have, in your lab, a box with several
serial ports on it, where you connect the console ports of embedded
devices, development boards, etc. Let's call it "termbox". You want to
access these console ports remotely.

If you provide shell-access to termbox for your users, then it's as
simple as having the users say (from their remote workstations):

    $ ssh -t user@termbox picocom -b 115200 /dev/ttyS0

Or make a convenient script/alias for this. Remember the `-t` switch
which instructs ssh to create a pseudo-tty, otherwise picocom won't
work.

What if you *don't* want to give users shell-access to termbox? Then
you can use picocom in a setup like the one described below. Just
remember, there are countless variations to this theme, the one below
is just one of them. Also, keep in mind that some of the commands
shown may have small differences from system to system; more so if you
go from Linux to other Unix-like systems.

Login to termbox and create a user called _termbox_:

    $ sudo useradd -r -m termbox

The `-r` means "system account", and the `-m` means *do* make the
home-directory. Mostly we need this account's home-directory as a
convenient place to keep stuff; so it doesn't need a login shell or a
password.

Switch to the _termbox_ account and create a `bin` directory in its
home-dir.

    $ sudo su termbox
    $ cd ~
    $ mkdir bin

Copy the picocom binary in `~termbox/bin` (if you don't have it
globally installed):

    $ cp /path/to/picocom ./bin

For every serial port you want to provide access to, create a file
named after the port and put it in `~termbox/bin`. It should look like
this:

    $ cat ./bin/ttyS0
    #!/bin/sh
    exec /home/termbox/bin/picocom \
      --send-cmd '' \
      --receive-cmd '' \
      -b 115200 \
      /dev/ttyS0

And make it executable:

    $ chmod +x ./bin/ttyS0

Repeat accordingly for every other port. Now the contents of
`~termbox/bin` should look like this:

    $ ls -l ./bin
    -rwxrwxr-x 1 termbox termbox 102128 Aug 29 13:56 picocom*
    -rwxrwxr-x 1 termbox termbox    108 Aug 29 14:07 ttyS0*
    -rwxrwxr-x 1 termbox termbox    108 Aug 29 14:07 ttyS1*
    ... and so on ...

Exit the _termbox_ account:

    $ exit

Now, for every serial port, create a user account named after the
port, like this:

    $ sudo useradd -r -g dialout -d ~termbox -M -s ~termbox/bin/ttyS0 ttyS0

Observe that we make `dialout` the default group for this account, so
the account has access to the serial ports. Also observe that we make
the script we just wrote (`~termbox/bin/ttyS0`) the login-shell for
the account. The `-d` option instructs useradd to use `/home/termbox`
as the user's home directory, and the `-M` switch instructs it *not*
to create the home-directory. We don't really need a home directory
for the _ttyS0_ account, since picocom will not read or write any
files; but we provide one, regardless, because *some* systems need a
valid home-directory to cd-into on login (else they choke). We could
as well have used `/` as the home directory, or we could have let
useradd create the usual `/home/ttyS0`.

Then set a password for the newly created account:

    $ sudo passwd ttyS0
    Enter new UNIX password: ******
    Retype new UNIX password: ******

Repeat (create user account, set password) for every port you want to
give access to.

You 're set. All a user has to do to remotely access the console
connected to termbox's `/dev/ttyS0` port, is:

    ssh ttyS0@termbox

Some interesting points:

- If the default port settings you specified as command-line arguments
  to picocom in `~termbox/bin/ttySx` do not match the settings of the
  device connected to the port, the user can easily change them from
  within picocom, using picocom commands.

- If a second user tries to remotely access the same port, at the same
  time, picocom won't let him (picocom will find the port locked and
  exit).

- In the example `~termbox/bin/ttySx` scripts we have completely
  disabled the send- and receive-file picocom commands. This
  guarantees that picocom won't execute any external commands. If you
  want, you can enable the commands by providing specific file-upload
  and file-download programs as the arguments to the `--send-cmd` and
  `--receive-cmd` picocom command-line options (provided, of-course,
  that you trust these programs). Picocom (starting with release 2.0)
  does not use `/bin/sh` to execute the file-upload and file-download
  programs and *will not* let the user inject shell-commands when
  supplying additional arguments to them.

- If you allow send- and receive-file operations as described above,
  you will, most likely, also need a way for your users to put files
  on termbox, and get files back from it. There are many ways to
  arrange for this, but they are beyond the scope of this simple
  example.

Again, this is only *one* possible setup. There are countless other
variations and elaborations you can try. Be creative!
