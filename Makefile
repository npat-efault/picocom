
VERSION=2.0a

#CC = gcc
CPPFLAGS=-DVERSION_STR=\"$(VERSION)\"
CFLAGS = -Wall -g

#LD = gcc
LDFLAGS = -g
LDLIBS =

## Increase this to use larger input (e.g. copy-paste) buffer
TTY_Q_SZ = 1024
CPPFLAGS += -DTTY_Q_SZ=$(TTY_Q_SZ)

## Comment this out to disable high-baudrate support
CPPFLAGS += -DHIGH_BAUD

## Normally you should NOT enable both: UUCP-style and flock(2)
## locking.

## Comment this out to disable locking with flock
CPPFLAGS += -DUSE_FLOCK

## Comment these out to disable UUCP-style lockdirs
#UUCP_LOCK_DIR=/var/lock
#CPPFLAGS += -DUUCP_LOCK_DIR=\"$(UUCP_LOCK_DIR)\"

## Comment these out to disable "linenoise"-library support
HISTFILE = .picocom_history
CPPFLAGS += -DHISTFILE=\"$(HISTFILE)\" \
	    -DLINENOISE
picocom : linenoise-1.0/linenoise.o
linenoise-1.0/linenoise.o : linenoise-1.0/linenoise.c linenoise-1.0/linenoise.h

## Comment these IN to enable custom baudrate support.
## Currently works *only* with Linux (kernels > 2.6).
CPPFLAGS += -DUSE_CUSTOM_BAUD
picocom : termios2.o
termios2.o : termios2.c termios2.h termbits2.h

## Comment this IN to remove help strings (saves ~ 4-6 Kb).
#CPPFLAGS += -DNO_HELP


picocom : picocom.o term.o fdio.o split.o
#	$(LD) $(LDFLAGS) -o $@ $+ $(LDLIBS)

picocom.o : picocom.c term.h
term.o : term.c term.h
split.o : split.c split.h
fdio.o : fdio.c fdio.h


doc : picocom.8 picocom.8.html picocom.8.ps

changes :
	svn log -v . > CHANGES

picocom.8 : picocom.8.xml
	xmltoman $< > $@

picocom.8.html : picocom.8.xml
	xmlmantohtml $< > $@

picocom.8.ps : picocom.8
	groff -mandoc -Tps $< > $@

clean:
	rm -f picocom.o term.o fdio.o split.o linenoise-1.0/linenoise.o
	rm -f termios2.o
	rm -f *~
	rm -f \#*\#

distclean: clean
	rm -f picocom

realclean: distclean
	rm -f picocom.8
	rm -f picocom.8.html
	rm -f picocom.8.ps
	rm -f CHANGES
