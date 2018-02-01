#
# 2017, Christopher N. Hesse <christopher.hesse@delphi.com>
#

LOCAL_PATH := $(call my-dir)

VERSION := 3.1
TTY_Q_SZ := 0
HISTFILE := .picocom_history


##########################################################################
# linenoise

include $(CLEAR_VARS)

LOCAL_SRC_FILES := linenoise-1.0/linenoise.c

LOCAL_MODULE := linenoise

include $(BUILD_STATIC_LIBRARY)

##########################################################################
# picocom

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    picocom.c \
    term.c \
    fdio.c \
    split.c \
    termios2.c

LOCAL_CFLAGS += -DVERSION_STR=\"$(VERSION)\"

LOCAL_CFLAGS += -DTTY_Q_SZ=$(TTY_Q_SZ)

## Comment this out to disable high-baudrate support
LOCAL_CFLAGS += -DHIGH_BAUD

## Normally you should NOT enable both: UUCP-style and flock(2)
## locking.

## Comment this out to disable locking with flock
LOCAL_CFLAGS += -DUSE_FLOCK

## Comment these out to disable UUCP-style lockdirs
#UUCP_LOCK_DIR=/var/lock
#LOCAL_CFLAGS += -DUUCP_LOCK_DIR=\"$(UUCP_LOCK_DIR)\"

## Comment these out to disable "linenoise"-library support
LOCAL_CFLAGS += -DHISTFILE=\"$(HISTFILE)\" -DLINENOISE

LOCAL_STATIC_LIBRARIES += linenoise

LOCAL_MODULE := picocom

include $(BUILD_EXECUTABLE)
