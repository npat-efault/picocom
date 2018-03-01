/* vi: set sw=4 ts=4:
 *
 * ttylocal.c
 *
 * General purpose terminal handling library. Local (native) tty
 * handling.
 *
 * by Nick Patavalis (npat@efault.net)
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
 *
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>

/* On these systems, use the TIOCM[BIS|BIC|GET] ioctls to manipulate
 * the modem control lines (DTR / RTS) */
#if defined(__linux__) || \
    defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__) || \
    defined(__APPLE__)
#define USE_IOCTL
#endif
#ifdef USE_IOCTL
#include <sys/ioctl.h>
#endif

#include "custbaud.h"
#ifdef USE_CUSTOM_BAUD
#include CUSTOM_BAUD_HEAD
#endif

/* Time to wait for UART to clear after a drain (in usec). */
#define DRAIN_DELAY 200000

#include "term.h"
#include "termint.h"

/***************************************************************************/

static int
ttylocal_init(struct term_s *t)
{
    int rval = 0;
    const char *n;

    do { /* dummy */
        if ( ! isatty(t->fd) ) {
            term_errno = TERM_EISATTY;
            rval = -1;
            break;
        }
        if ( ! t->name ) {
            n = ttyname(t->fd);
            if ( n ) t->name = strdup(n);
        }
    } while (0);

    return rval;
}

static int
ttylocal_tcgetattr(struct term_s *t, struct termios *termios_out)
{
    int r;

    r = tcgetattr(t->fd, termios_out);
    if ( r < 0 ) term_errno = TERM_EGETATTR;
    return r;
}

static int
ttylocal_tcsetattr(struct term_s *t, int when, const struct termios *termios)
{
    int r;

    do {
        r = tcsetattr(t->fd, when, termios);
    } while ( r < 0 && errno == EINTR );
    if ( r < 0 ) term_errno = TERM_ESETATTR;
    return r;
}

#ifdef USE_IOCTL

static int
sys2term (int sys)
{
    int term = 0;
    if (sys & TIOCM_DTR) term |= MCTL_DTR;
    if (sys & TIOCM_DSR) term |= MCTL_DSR;
    if (sys & TIOCM_CD) term |= MCTL_DCD;
    if (sys & TIOCM_RTS) term |= MCTL_RTS;
    if (sys & TIOCM_CTS) term |= MCTL_CTS;
    if (sys & TIOCM_RI) term |= MCTL_RI;
    return term;
}

static int
term2sys (int term)
{
    int sys = 0;
    if (term & MCTL_DTR) sys |= TIOCM_DTR;
    if (term & MCTL_DSR) sys |= TIOCM_DSR;
    if (term & MCTL_DCD) sys |= TIOCM_CD;
    if (term & MCTL_RTS) sys |= TIOCM_RTS;
    if (term & MCTL_CTS) sys |= TIOCM_CTS;
    if (term & MCTL_RI) sys |= TIOCM_RI;
    return sys;
}

static int
ttylocal_modem_get(struct term_s *t, int *pins)
{
    int r, syspins;

    r = ioctl(t->fd, TIOCMGET, &syspins);
    if ( r < 0 ) { term_errno = TERM_EGETMCTL; return r; }
    *pins = sys2term(syspins);
    return 0;
}

static int
ttylocal_modem_bis(struct term_s *t, const int *pins)
{
    int r, syspins;

    syspins = term2sys(*pins);
    r = ioctl(t->fd, TIOCMBIS, &syspins);
    if ( r < 0 ) term_errno = TERM_ESETMCTL;
    return r;
}

static int
ttylocal_modem_bic(struct term_s *t, const int *pins)
{
    int r, syspins;

    syspins = term2sys(*pins);
    r = ioctl(t->fd, TIOCMBIC, &syspins);
    if ( r < 0 ) term_errno = TERM_ESETMCTL;
    return r;
}

#endif /* of USE_IOCTL */

static int
ttylocal_send_break(struct term_s *t)
{
    int r;
    do {
        r = tcsendbreak(t->fd, 0);
    } while (r < 0 && errno == EINTR );
    if ( r < 0 ) term_errno = TERM_EBREAK;
    return r;
}

static int
ttylocal_flush(struct term_s *t, int selector)
{
    int r;

    r = tcflush(t->fd, selector);
    if ( r < 0 ) term_errno = TERM_EFLUSH;
    return r;
}

static int
ttylocal_fake_flush(struct term_s *t)
{
    struct termios tio, tio_orig;
    int term_errno_hold = 0, errno_hold = 0;
    int r, rval = 0;

    do { /* dummy */
        /* Get current termios */
        r = t->ops->tcgetattr(t, &tio);
        if ( r < 0 ) {
            rval = -1;
            break;
        }
        tio_orig = tio;
        /* Set flow-control to none */
        tio.c_cflag &= ~(CRTSCTS);
        tio.c_iflag &= ~(IXON | IXOFF | IXANY);
        /* Apply termios */
        r = t->ops->tcsetattr(t, TCSANOW, &tio);
        if ( r < 0 ) {
            rval = -1;
            break;
        }
        /* Wait for output to drain. Without flow-control this should
           complete in finite time. */
        r = t->ops->drain(t);
        if ( r < 0 ) {
            term_errno_hold = term_errno;
            errno_hold = errno;
            rval = -1;
            /* continue */
        }
        /* Reset flow-control to original setting. */
        r = t->ops->tcsetattr(t, TCSANOW, &tio_orig);
        if ( r < 0 ) {
            rval = -1;
            break;
        }

    } while(0);

    if ( term_errno_hold ) {
        term_errno = term_errno_hold;
        errno = errno_hold;
    }

    return rval;
}

static int
ttylocal_drain(struct term_s *t)
{
    int r;

    do {
#ifdef __BIONIC__
        /* See: http://dan.drown.org/android/src/gdb/no-tcdrain */
        r = ioctl(t->fd, TCSBRK, 1);
#else
        r = tcdrain(t->fd);
#endif
    } while ( r < 0 && errno == EINTR);
    if ( r < 0 ) {
        term_errno = TERM_EDRAIN;
        return r;
    }

    /* Give some time to the UART to transmit everything. Some
       systems and / or drivers corrupt the last character(s) if
       the port is immediately reconfigured, even after a
       drain. (I guess, drain does not wait for everything to
       actually be transitted on the wire). */
    if ( DRAIN_DELAY ) usleep(DRAIN_DELAY);

    return 0;
}

int
ttylocal_read(struct term_s *t, void *buf, unsigned bufsz)
{
    int r;
    r = read(t->fd, buf, bufsz);
    if ( r < 0 ) term_errno = TERM_EINPUT;
    return r;
}

int
ttylocal_write(struct term_s *t, const void *buf, unsigned bufsz)
{
    int r;
    r = write(t->fd, buf, bufsz);
    if ( r < 0 ) term_errno = TERM_EOUTPUT;
    return r;
}

const struct term_ops ttylocal_term_ops = {
    .init = ttylocal_init,
    .fini = NULL,
    .tcgetattr = ttylocal_tcgetattr,
    .tcsetattr = ttylocal_tcsetattr,
#ifdef USE_IOCTL
    .modem_get = ttylocal_modem_get,
    .modem_bis = ttylocal_modem_bis,
    .modem_bic = ttylocal_modem_bic,
#else
    .modem_get = NULL,
    .modem_bis = NULL,
    .modem_bic = NULL,
#endif
    .send_break = ttylocal_send_break,
    .flush = ttylocal_flush,
    .fake_flush = ttylocal_fake_flush,
    .drain = ttylocal_drain,
    .read = ttylocal_read,
    .write = ttylocal_write,
};

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
