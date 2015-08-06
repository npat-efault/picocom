
VERSION=1.8

# CC = gcc
CPPFLAGS=-DVERSION_STR=\"$(VERSION)\"
CFLAGS = -Wall -g

# LD = gcc
LDFLAGS = -g
LDLIBS =

## Comment this out to disable high-baudrate support
CPPFLAGS += -DHIGH_BAUD

## Comment these out to disable UUCP-style lockdirs
UUCP_LOCK_DIR=/var/lock
CPPFLAGS += -DUUCP_LOCK_DIR=\"$(UUCP_LOCK_DIR)\"

## Comment these out to disable "linenoise"-library support
SEND_RECEIVE_HISTFILE = .picocom_send_receive
CPPFLAGS += -DSEND_RECEIVE_HISTFILE=\"$(SEND_RECEIVE_HISTFILE)\" \
	    -DLINENOISE
picocom : linenoise-1.0/linenoise.o
linenoise-1.0/linenoise.o : linenoise-1.0/linenoise.c linenoise-1.0/linenoise.h


picocom : picocom.o term.o
#	$(LD) $(LDFLAGS) -o $@ $+ $(LDLIBS)

picocom.o : picocom.c term.h
term.o : term.c term.h


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
	rm -f picocom.o term.o linenoise-1.0/linenoise.o
	rm -f *~
	rm -f \#*\#

distclean: clean
	rm -f picocom

realclean: distclean
	rm -f picocom.8
	rm -f picocom.8.html
	rm -f picocom.8.ps
	rm -f CHANGES
