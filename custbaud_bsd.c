/*
 * custbaud_bsd.c
 *
 * Custom baud rate support for BSD and macOS.
 *
 * by Joe Merten (https://github.com/JoeMerten www.jme.de)
 *
 * ATTENTION: BSD and macOS specific stuff!
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/***************************************************************************/
/* Known issues:
 * - FT232H 12MBaud not working
 *   - using OSX El Capitan 10.11.6, FTDIUSBSerialDriver_v2_3.dmg
 *   - tried with 2 different chips (FT232H and FT2232H)
 *   Testing with Ftdi FT232H, which is capable to use up to 12MBaud, only
 *   line speed up to 3MBaud were accepted. For higher baudrates we earn
 *   a failure in the ioctl(IOSSIOSPEED) call.
 *   But as `python -m serial.tools.miniterm` shows the same behaviour, it
 *   looks that this is a bug or limitation in OSX and/or Ftdi driver.
 *   Trying with PL2303 (driver version PL2303_MacOSX_1.6.1_20160309.zip),
 *   baudrates up to 6MBaud were accepted.
 *   - Have not tested with more recent macOS or Ftdi driver until now.
 */

#include "custbaud.h"

/* Note that this code might also work with other BSD variants, but I have only
 * tested with those listed below. Also tested __NetBSD__ but won't work. */
#if (defined (__FreeBSD__) || defined (__NetBSD__) ||       \
     defined (__OpenBSD__) || defined (__DragonFly__) ||    \
     defined (__APPLE__)) &&                                \
    defined (USE_CUSTOM_BAUD)

#include "custbaud_bsd.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif
#include "term.h"

/***************************************************************************/
/* As we can see in BSD and macOS termios.h all the baudrate constants are
 * transparent, like B115200=115200. There is no need for any integer <-> code
 * translation. So we can pass any baudrate we want directly to / from
 * cfsetospeed() & co.
 */
int cfsetospeed_custom(struct termios *tiop, int speed) {
    return cfsetospeed(tiop, speed);
}

int cfsetispeed_custom(struct termios *tiop, int speed) {
    return cfsetispeed(tiop, speed);
}

int cfgetospeed_custom(const struct termios *tiop) {
    return cfgetospeed(tiop);
}

int cfgetispeed_custom(const struct termios *tiop) {
    return cfgetispeed(tiop);
}

#ifdef __APPLE__
/***************************************************************************/
/* Need to undef tcsetattr to get access to the original tcsetattr()
 * function inside our module.
 */
#undef tcsetattr

/***************************************************************************/
/* The strategy of picocom's terminal handling library is to hold all the
 * terminal settings (including baudrate) using termios struct.
 * Problem on macOS is, that tcsetattr() will fail if termios contains an
 * unusual baudrate (like e.g. 12345 of 12M), The official macOS way to apply
 * those baudrates is to use ioctl(IOSSIOSPEED) instead.
 * Our workaround strategy is:
 * - set the baudrate stored in termios back to a standard value (e.g. 9600)
 * - call tcsetattr() to apply all the rest termios data to the fd
 * - and then applying the real desired baudrate to the fd by calling ioctl(IOSSIOSPEED)
 * Note, that in case of failed ioctl(IOSSIOSPEED), our 9600 staying
 * configured at the fd.
 */
int tcsetattr_custom(int fd, int optional_actions, const struct termios *tiop) {
    int r;
    int workaround = 0;
    int baudrate;
    struct termios tios = *tiop;
    struct termios tio0;
    int baudrate0;

    if ( fd >= 3 ) { /* don't apply this workaround for stdin/stdout/stderr */
        baudrate = cfgetospeed(&tios);
        if (baudrate > 460800 || !term_baud_std(baudrate)) {
            /* save fd's current termios to recover in case of later falure */
            r = tcgetattr(fd, &tio0);
            if ( r < 0 ) return -1;
            baudrate0 = cfgetospeed(&tio0);
            /* now temporarily switching baudrate back to 9600 */
            r = cfsetspeed(&tios, B9600);
            if ( r < 0 ) return -1;
            workaround = 1;
        }
    }

    r = tcsetattr(fd, optional_actions, &tios);
    if ( r < 0 ) return -1;

    if ( workaround ) {
        r = ioctl(fd, IOSSIOSPEED, &baudrate);
        /*if ( r < 0 ) fprintf(stderr, "%s: ioctl(%d, %d) = %d, optional_actions = %d, %s\r\n", __FUNCTION__, fd, baudrate, r, optional_actions, strerror(errno));*/
        if ( r < 0 ) {
            /* ioctl() failed, so we try to restore the fd to the old termios data */
            r = cfsetspeed(&tio0, B9600);
            /*if ( r < 0 ) fprintf(stderr, "%s: cfsetspeed() = %d, %s\r\n", __FUNCTION__, r, strerror(errno));*/
            if ( r < 0 ) return -1;
            r = tcsetattr(fd, optional_actions, &tio0);
            /*if ( r < 0 ) fprintf(stderr, "%s: tcsetattr() = %d, %s\r\n", __FUNCTION__, r, strerror(errno));*/
            if ( r < 0 ) return -1;
            r = ioctl(fd, IOSSIOSPEED, &baudrate0);
            /*if ( r < 0 ) fprintf(stderr, "%s: ioctl(%d) = %d, %s\r\n", __FUNCTION__, baudrate0, r, strerror(errno));*/
            return -1;
        }
    }

    return 0;
}
#endif /*__APPLE__ */

/***************************************************************************/

#endif /* __FreeBSD__ || ... || __APPLE__ && USE_CUSTOM_BAUD */
