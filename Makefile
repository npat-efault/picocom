
VERSION = 2.2a

#CC = gcc
CPPFLAGS = -DVERSION_STR=\"$(VERSION)\"
CFLAGS = -Wall -g

LD = $(CC)
LDFLAGS = -g
LDLIBS =

all: picocom
OBJS =

## Increase this to use larger input (e.g. copy-paste) buffer
TTY_Q_SZ = 32768
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
OBJS += linenoise-1.0/linenoise.o
linenoise-1.0/linenoise.o : linenoise-1.0/linenoise.c linenoise-1.0/linenoise.h

## Enable custom baudrate support only on Linux > 2.6
UNAME_S = $(shell uname -s)
ifeq ($(UNAME_S), Linux)
MINIMAL_KVER = 2.6.0
UNAME_R = $(shell uname -r)
LOWER_KVER = $(shell echo -e "$(UNAME_R)\n$(MINIMAL_KVER)" | sort -V | head --lines=1)
ifeq ($(LOWER_KVER), $(MINIMAL_KVER))
$(info Building on Linux > 2.6. Enabling custom baud rate support.)
CPPFLAGS += -DUSE_CUSTOM_BAUD
OBJS += termios2.o
termios2.o : termios2.c termios2.h termbits2.h
endif
endif

## Comment this IN to remove help strings (saves ~ 4-6 Kb).
#CPPFLAGS += -DNO_HELP


OBJS += picocom.o term.o fdio.o split.o
picocom : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

picocom.o : picocom.c term.h
term.o : term.c term.h
split.o : split.c split.h
fdio.o : fdio.c fdio.h

.c.o :
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<


doc : picocom.1.html picocom.1 picocom.1.pdf

picocom.1 : picocom.1.md
	sed 's/\*\*\[/\*\*/g;s/\]\*\*/\*\*/g' $? \
	| pandoc -s -t man \
            -Vfooter="Picocom $(VERSION)" -Vdate="`date -I`" \
	    -o $@

picocom.1.html : picocom.1.md
	pandoc -s -t html \
	    -c css/normalize-noforms.css -c css/manpage.css \
            --self-contained \
	    -o $@ $?

picocom.1.pdf : picocom.1
	groff -man -Tpdf $? > $@


clean:
	rm -f picocom.o term.o fdio.o split.o
	rm -f linenoise-1.0/linenoise.o
	rm -f termios2.o
	rm -f *~
	rm -f \#*\#

distclean: clean
	rm -f picocom

realclean: distclean
	rm -f picocom.1
	rm -f picocom.1.html
	rm -f picocom.1.pdf
	rm -f CHANGES
