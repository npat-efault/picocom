/*
 * termios2.c
 *
 * Use termios2 interface to set custom baud rates to serial ports.
 *
 * by Nick Patavalis (npat@efault.net)
 *
 * ATTENTION: Linux-specific kludge!
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

#include "custbaud.h"

#if defined(__linux__) && defined(USE_CUSTOM_BAUD)

#include <linux/version.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,0)
#error "This code requires Linux kernel > 2.6!"
#endif

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

/* Contains the definition of the termios2 structure and some related
   constants that we should normally include from system
   headers. Unfortunatelly, we can't. See comments in "termbits2.h"
   for more. */
#include "termbits2.h"

/* GLIBC termios use an (otherwise unused) bit in c_iflags to
   internally record the fact that ispeed was set to zero (which is
   special behavior and means "same as ospeed". We want to clear this
   bit before passing c_iflags back to the kernel. See:

       <glibc-source>/sysdeps/unix/sysv/linux/speed.c
*/
#define IBAUD0 020000000000

int
tc2setattr(int fd, int optional_actions, const struct termios *tios)
{
    struct termios2 t2;
    int cmd;

    if ( ! use_custom_baud() ) return tcsetattr(fd, optional_actions, tios);

    switch (optional_actions) {
    case TCSANOW:
        cmd = IOCTL_SETS;
        break;
    case TCSADRAIN:
        cmd = IOCTL_SETSW;
        break;
    case TCSAFLUSH:
        cmd = IOCTL_SETSF;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    t2.c_iflag = tios->c_iflag & ~IBAUD0;
    t2.c_oflag = tios->c_oflag;
    t2.c_cflag = tios->c_cflag;
    t2.c_lflag = tios->c_lflag;
    t2.c_line = tios->c_line;
    t2.c_ispeed = tios->c_ispeed;
    t2.c_ospeed = tios->c_ospeed;
    memcpy(&t2.c_cc[0], &tios->c_cc[0], K_NCCS * sizeof (cc_t));

    return ioctl(fd, cmd, &t2);
}

int
tc2getattr(int fd, struct termios *tios)
{
    struct termios2 t2;
    size_t i;
    int r;

    if ( ! use_custom_baud() ) return tcgetattr(fd, tios);

    r = ioctl(fd, IOCTL_GETS, &t2);
    if (r < 0) return r;

    tios->c_iflag = t2.c_iflag;
    tios->c_oflag = t2.c_oflag;
    tios->c_cflag = t2.c_cflag;
    tios->c_lflag = t2.c_lflag;
    tios->c_line = t2.c_line;
    tios->c_ispeed = t2.c_ispeed;
    tios->c_ospeed = t2.c_ospeed;
    memcpy(&tios->c_cc[0], &t2.c_cc[0], K_NCCS * sizeof (cc_t));

    for (i = K_NCCS; i < NCCS; i++)
        tios->c_cc[i] = _POSIX_VDISABLE;

    return 0;
}

/* The termios2 interface supports separate input and output
   speeds. GLIBC's termios support only one terminal speed. So the
   standard tcsetispeed(3), actually sets the output-speed field, not
   the input-speed field (or does nothing if speed == B0). Use
   cf2setispeed if you want to set a *standard* input speed (one of
   the Bxxxxx speeds) that may be different from the output
   speed. Also if someone, somehow, has set the input speed to
   something other than B0, then you *must* use cf2setispeed() to
   change it. Using the standard cfsetispeed() obviously won't do
   (since it affects only the output-speed field).
*/

int
cf2setispeed(struct termios *tios, speed_t speed)
{
    if ( ! use_custom_baud() ) return cfsetispeed(tios, speed);

    if ( (speed & ~CBAUD) != 0
         && (speed < B57600 || speed > __MAX_BAUD) ) {
        errno = EINVAL;
        return -1;
    }
    tios->c_ispeed = speed;
    tios->c_cflag &= ~((CBAUD | CBAUDEX) << IBSHIFT);
    tios->c_cflag |= (speed << IBSHIFT);

    return 0;
}

speed_t
cf2getispeed(const struct termios *tios)
{
    if ( ! use_custom_baud() ) return cfgetispeed(tios);

    return (tios->c_cflag >> IBSHIFT) & (CBAUD | CBAUDEX);
}

/* Use these to set custom input or output speeds (i.e. speeds that do
   not necessarily correspond to one of the Bxxx macros. */

int
cf2setospeed_custom(struct termios *tios, int speed)
{
    if ( ! use_custom_baud() ) { errno = EINVAL; return -1; }

    if ( speed <= 0 ) {
        errno = EINVAL;
        return -1;
    }
    tios->c_cflag &= ~(CBAUD | CBAUDEX);
    tios->c_cflag |= BOTHER;
    tios->c_ospeed = speed;

    return 0;
}

int
cf2setispeed_custom(struct termios *tios, int speed)
{
    if ( ! use_custom_baud() ) { errno = EINVAL; return -1; }

    if ( speed < 0 ) {
        errno = EINVAL;
        return -1;
    }
    if ( speed == 0 ) {
        /* Special case: ispeed == 0 means "same as ospeed". Kernel
           does this if it sees B0 in the "CIBAUD" field (i.e. in
           CBAUD << IBSHIFT) */
        tios->c_cflag &= ~((CBAUD | CBAUDEX) << IBSHIFT);
        tios->c_cflag |= (B0 << IBSHIFT);
    } else {
        tios->c_cflag &= ~((CBAUD | CBAUDEX) << IBSHIFT);
        tios->c_cflag |= (BOTHER << IBSHIFT);
        tios->c_ispeed = speed;
    }

    return 0;
}

/***************************************************************************/

#endif /* __linux__ && USE_CUSTOM_BAUD */

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
