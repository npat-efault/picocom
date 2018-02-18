/* vi: set sw=4 ts=4:
 *
 * term.c
 *
 * General purpose terminal handling library.
 *
 * by Nick Patavalis (npat@efault.net)
 *
 * originaly by Pantelis Antoniou (https://github.com/pantoniou),
 *              Nick Patavalis
 *
 * Documentation can be found in the header file "term.h".
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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#ifdef USE_FLOCK
#include <sys/file.h>
#endif

/* glibc for MIPS has its own bits/termios.h which does not define
 * CMSPAR, so we use the value from the generic bits/termios.h
 */
#ifdef __linux__
#ifndef CMSPAR
#define CMSPAR 010000000000
#endif
#endif

/* Some BSDs (and possibly other systems too) have no mark / space
 * parity support, and they don't define CMSPAR. Use a zero CMSPAR in
 * these cases. If the user tries to set P_MARK or P_SPACE he will get
 * P_EVEN or P_ODD instead. */
#ifndef CMSPAR
#define CMSPAR 0
#endif

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

static int term_initted;

static struct term_s term[MAX_TERMS];

/***************************************************************************/

static int
local_init(struct term_s *t)
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
local_tcgetattr(struct term_s *t, struct termios *termios_out)
{
    return tcgetattr(t->fd, termios_out);
}

static int
local_tcsetattr(struct term_s *t, int when, const struct termios *termios)
{
    return tcsetattr(t->fd, when, termios);
}

static int
local_modem_get(struct term_s *t, int *modem_out)
{
    return ioctl(t->fd, TIOCMGET, modem_out);
}

static int
local_modem_bis(struct term_s *t, const int *modem)
{
    return ioctl(t->fd, TIOCMBIS, modem);
}

static int
local_modem_bic(struct term_s *t, const int *modem)
{
    return ioctl(t->fd, TIOCMBIC, modem);
}

static int
local_send_break(struct term_s *t)
{
    return tcsendbreak(t->fd, 0);
}

static int
local_flush(struct term_s *t, int selector)
{
    return tcflush(t->fd, selector);
}

static int
local_drain(struct term_s *t)
{
    int r;

#ifdef __BIONIC__
    /* See: http://dan.drown.org/android/src/gdb/no-tcdrain */
    r = ioctl(t->fd, TCSBRK, 1);
#else
    r = tcdrain(t->fd);
#endif
    return r;
}

int
local_read(struct term_s *t, void *buf, unsigned bufsz)
{
    return read(t->fd, buf, bufsz);
}

int
local_write(struct term_s *t, const void *buf, unsigned bufsz)
{
    return write(t->fd, buf, bufsz);
}

static const struct term_ops local_term_ops = {
    .init = local_init,
    .tcgetattr = local_tcgetattr,
    .tcsetattr = local_tcsetattr,
    .modem_get = local_modem_get,
    .modem_bis = local_modem_bis,
    .modem_bic = local_modem_bic,
    .send_break = local_send_break,
    .flush = local_flush,
    .drain = local_drain,
    .read = local_read,
    .write = local_write,
};

/***************************************************************************/

int term_errno;

static const char * const term_err_str[] = {
    [TERM_EOK]        = "No error",
    [TERM_ENOINIT]    = "Framework is uninitialized",
    [TERM_EFULL]      = "Framework is full",
    [TERM_ENOTFOUND]  = "Filedes not in the framework",
    [TERM_EEXISTS]    = "Filedes already in the framework",
    [TERM_EATEXIT]    = "Cannot install atexit handler",
    [TERM_EISATTY]    = "Filedes is not a tty",
    [TERM_EFLUSH]     = "Cannot flush the device",
    [TERM_EGETATTR]   = "Cannot get the device attributes",
    [TERM_ESETATTR]   = "Cannot set the device attributes",
    [TERM_EBAUD]      = "Invalid baud rate",
    [TERM_ESETOSPEED] = "Cannot set the output speed",
    [TERM_ESETISPEED] = "Cannot set the input speed",
    [TERM_EGETSPEED]  = "Cannot decode speed",
    [TERM_EPARITY]    = "Invalid parity mode",
    [TERM_EDATABITS]  = "Invalid number of databits",
    [TERM_ESTOPBITS]  = "Invalid number of stopbits",
    [TERM_EFLOW]      = "Invalid flowcontrol mode",
    [TERM_EDTRDOWN]   = "Cannot lower DTR",
    [TERM_EDTRUP]     = "Cannot raise DTR",
    [TERM_EMCTL]      = "Cannot get mctl status",
    [TERM_EDRAIN]     = "Cannot drain the device",
    [TERM_EBREAK]     = "Cannot send break sequence",
    [TERM_ERTSDOWN]   = "Cannot lower RTS",
    [TERM_ERTSUP]     = "Cannot raise RTS"
};

static char term_err_buff[1024];

const char *
term_strerror (int terrnum, int errnum)
{
    const char *rval;

    switch(terrnum) {
    case TERM_EFLUSH:
    case TERM_EGETATTR:
    case TERM_ESETATTR:
    case TERM_ESETOSPEED:
    case TERM_ESETISPEED:
    case TERM_EDRAIN:
    case TERM_EBREAK:
        snprintf(term_err_buff, sizeof(term_err_buff),
                 "%s: %s", term_err_str[terrnum], strerror(errnum));
        rval = term_err_buff;
        break;
    case TERM_EOK:
    case TERM_ENOINIT:
    case TERM_EFULL:
    case TERM_ENOTFOUND:
    case TERM_EEXISTS:
    case TERM_EATEXIT:
    case TERM_EISATTY:
    case TERM_EBAUD:
    case TERM_EPARITY:
    case TERM_EDATABITS:
    case TERM_ESTOPBITS:
    case TERM_EFLOW:
    case TERM_EDTRDOWN:
    case TERM_EDTRUP:
    case TERM_EMCTL:
    case TERM_ERTSDOWN:
    case TERM_ERTSUP:
        snprintf(term_err_buff, sizeof(term_err_buff),
                 "%s", term_err_str[terrnum]);
        rval = term_err_buff;
        break;
    default:
        rval = NULL;
        break;
    }

    return rval;
}

int
term_perror (const char *prefix)
{
    return fprintf(stderr, "%s %s\n",
                   prefix, term_strerror(term_errno, errno));
}

/***************************************************************************/

#define BNONE 0xFFFFFFFF

struct baud_codes {
    int speed;
    speed_t code;
} baud_table[] = {
    { 0, B0 },
    { 50, B50 },
    { 75, B75 },
    { 110, B110 },
    { 134, B134 },
    { 150, B150 },
    { 200, B200 },
    { 300, B300 },
    { 600, B600 },
    { 1200, B1200 },
    { 1800, B1800 },
    { 2400, B2400 },
    { 4800, B4800 },
    { 9600, B9600 },
    { 19200, B19200 },
    { 38400, B38400 },
    { 57600, B57600 },
    { 115200, B115200 },
#ifdef HIGH_BAUD
#ifdef B230400
    { 230400, B230400 },
#endif
#ifdef B460800
    { 460800, B460800 },
#endif
#ifdef B500000
    { 500000, B500000 },
#endif
#ifdef B576000
    { 576000, B576000 },
#endif
#ifdef B921600
    { 921600, B921600 },
#endif
#ifdef B1000000
    { 1000000, B1000000 },
#endif
#ifdef B1152000
    { 1152000, B1152000 },
#endif
#ifdef B1500000
    { 1500000, B1500000 },
#endif
#ifdef B2000000
    { 2000000, B2000000 },
#endif
#ifdef B2500000
    { 2500000, B2500000 },
#endif
#ifdef B3000000
    { 3000000, B3000000 },
#endif
#ifdef B3500000
    { 3500000, B3500000 },
#endif
#ifdef B4000000
    { 4000000, B4000000 },
#endif
#endif /* of HIGH_BAUD */
};

#define BAUD_TABLE_SZ ((int)(sizeof(baud_table) / sizeof(baud_table[0])))

int
term_baud_up (int baud)
{
    int i;

    for (i = 0; i < BAUD_TABLE_SZ; i++) {
        if ( baud >= baud_table[i].speed )
            continue;
        else {
            baud = baud_table[i].speed;
            break;
        }
    }

    return baud;
}

int
term_baud_down (int baud)
{
    int i;

    for (i = BAUD_TABLE_SZ - 1; i >= 0; i--) {
        if ( baud <= baud_table[i].speed )
            continue;
        else {
            baud = baud_table[i].speed;
            break;
        }
    }

    return baud;
}

static speed_t
Bcode(int speed)
{
    speed_t code = BNONE;
    int i;

    for (i = 0; i < BAUD_TABLE_SZ; i++) {
        if ( baud_table[i].speed == speed ) {
            code = baud_table[i].code;
            break;
        }
    }
    return code;
}

static int
Bspeed(speed_t code)
{
    int speed = -1, i;

    for (i = 0; i < BAUD_TABLE_SZ; i++) {
        if ( baud_table[i].code == code ) {
            speed = baud_table[i].speed;
            break;
        }
    }
    return speed;
}

int
term_baud_ok(int baud)
{
#ifndef USE_CUSTOM_BAUD
    return (Bcode(baud) != BNONE) ? 1 : 0;
#else
    return (baud >= 0);
#endif
}

int
term_baud_std(int baud)
{
    return (Bcode(baud) != BNONE) ? 1 : 0;
}

/**************************************************************************/

static struct term_s *
term_new (int fd, const char *name, const struct term_ops *ops)
{
    int i;
    struct term_s *rval;

    do { /* dummy */
        if ( ! term_initted ) {
            term_errno = TERM_ENOINIT;
            rval = NULL;
            break;
        }

        for (i = 0; i < MAX_TERMS; i++)
            if ( term[i].fd == -1 ) break;

        if ( i == MAX_TERMS ) {
            term_errno = TERM_EFULL;
            rval = NULL;
            break;
        }

        rval = &term[i];
        memset(rval, 0, sizeof *rval);
        rval->fd = fd;
        rval->ops = ops;
        if ( name ) rval->name = strdup(name);

        if (ops->init) {
            int r = ops->init(rval);
            if ( r < 0 ) {
                /* Failed to init, abandon allocation */
                rval->fd = -1;
                if ( rval->name ) {
                    free(rval->name);
                    rval->name = NULL;
                }
                rval = NULL;
                break;
            }
        }

    } while (0);

    return rval;
}

static void
term_free (int fd)
{
    int i;

    for (i = 0; i < MAX_TERMS; i++) {
        if ( term[i].fd == fd ) {
            if (term[i].ops->fini)
                term[i].ops->fini(&term[i]);
            term[i].fd = -1;
            if ( term[i].name ) {
                free(term[i].name);
                term[i].name=NULL;
            }
            break;
        }
    }
}

/***************************************************************************/

static struct term_s *
term_find (int fd)
{
    int i;
    struct term_s *rval;

    do { /* dummy */
        if ( ! term_initted ) {
            term_errno = TERM_ENOINIT;
            rval = NULL;
            break;
        }

        for (i = 0; i < MAX_TERMS; i++)
            if (term[i].fd == fd) break;

        if ( i == MAX_TERMS ) {
            term_errno = TERM_ENOTFOUND;
            rval = NULL;
            break;
        }

        rval = &term[i];
    } while (0);

    return rval;
}

/***************************************************************************/

static void
term_exitfunc (void)
{
    int r, i;

    do { /* dummy */
        if ( ! term_initted )
            break;

        for (i = 0; i < MAX_TERMS; i++) {
            struct term_s *t = &term[i];
            if (t->fd == -1)
                continue;
            term_drain(t->fd);
            t->ops->flush(t, TCIFLUSH);
            do {
                r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
            } while ( r < 0 && errno == EINTR );
            if ( r < 0 ) {
                const char *tname;

                tname = t->name;
                if ( ! tname ) tname = "UNKNOWN";
                fprintf(stderr, "%s: reset failed for dev %s: %s\r\n",
                        __FUNCTION__, tname, strerror(errno));
            }
#ifdef USE_FLOCK
            /* Explicitly unlock the file. If the file is not in fact
               flock(2)'ed, no harm is done. This should normally not
               be necessary. Normally, exiting the program should take
               care of unlocking the file. Unfortuntelly, it has been
               observed that, on some systems, exiting or closing an
               flock(2)'ed tty fd has peculiar side effects (like not
               reseting the modem-control lines, even if HUPCL is
               set). */
            flock(t->fd, LOCK_UN);
#endif
            close(t->fd);
            term_free(t->fd);
        }
    } while (0);
}

/***************************************************************************/

int
term_lib_init (void)
{
    int rval, r, i;

    rval = 0;

    do { /* dummy */
        if ( term_initted ) {
            /* reset all terms back to their original settings */
            for (i = 0; i < MAX_TERMS; i++) {
                struct term_s *t = &term[i];
                if (t->fd == -1)
                    continue;
                t->ops->flush(t, TCIOFLUSH);
                do {
                    r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
                } while ( r < 0 && errno == EINTR );
                if ( r < 0 ) {
                    const char *tname;

                    tname = t->name;
                    if ( ! tname ) tname = "UNKNOWN";
                    fprintf(stderr, "%s: reset failed for dev %s: %s\n",
                            __FUNCTION__, tname, strerror(errno));
                }
                term_free(t->fd);
            }
        } else {
            /* initialize term structure. */
            for (i = 0; i < MAX_TERMS; i++)
                term[i].fd = -1;
            if ( atexit(term_exitfunc) != 0 ) {
                term_errno = TERM_EATEXIT;
                rval = -1;
                break;
            }
            /* ok. term struct is now initialized. */
            term_initted = 1;
        }
    } while(0);

    return rval;
}

/***************************************************************************/

int
term_add (int fd, const char *name, const struct term_ops *ops)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */
        t = term_find(fd);
        if ( t ) {
            term_errno = TERM_EEXISTS;
            rval = -1;
            break;
        }

        if ( ! ops )
            ops = &local_term_ops;

        t = term_new(fd, name, ops);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->tcgetattr(t, &t->origtermios);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            term_free(t->fd);
            break;
        }

        t->currtermios = t->origtermios;
        t->nexttermios = t->origtermios;
    } while (0);

    return rval;
}

/***************************************************************************/

int
term_remove(int fd)
{
    term_reset(fd);
    return term_erase(fd);
}

/***************************************************************************/

int
term_erase(int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */
        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        term_free(fd);
    } while (0);

    return rval;
}

/***************************************************************************/

int
term_replace (int oldfd, int newfd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(oldfd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        /* assert(t->fd == oldfd); */
        t->fd = newfd;

        r = t->ops->tcsetattr(t, TCSANOW, &t->currtermios);
        if ( r < 0 ) {
            term_errno = TERM_ESETATTR;
            rval = -1;
            t->fd = oldfd;
            break;
        }
        r = t->ops->tcgetattr(t, &t->currtermios);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            t->fd = oldfd;
            break;
        }

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_reset (int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->flush(t, TCIOFLUSH);
        if ( r < 0 ) {
            term_errno = TERM_EFLUSH;
            rval = -1;
            break;
        }
        r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
        if ( r < 0 ) {
            term_errno = TERM_ESETATTR;
            rval = -1;
            break;
        }
        r = t->ops->tcgetattr(t, &t->currtermios);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            break;
        }

        t->nexttermios = t->currtermios;
    } while (0);

    return rval;
}

/***************************************************************************/

int
term_revert (int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        t->nexttermios = t->currtermios;

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_refresh (int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->tcgetattr(t, &t->currtermios);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            break;
        }

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_apply (int fd, int now)
{
    int when, rval, r;
    struct term_s *t;

    when = now ? TCSANOW : TCSAFLUSH;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->tcsetattr(t, when, &t->nexttermios);
        if ( r < 0 ) {
            term_errno = TERM_ESETATTR;
            rval = -1;
            break;
        }
        r = t->ops->tcgetattr(t, &t->nexttermios);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            break;
        }

        t->currtermios = t->nexttermios;

        /* Set HUPCL to origtermios as well. Since setting HUPCL
           affects the behavior on close(2), we most likely want it to
           also apply when the filedes is implicitly closed by
           exit(3)ing the program. Since, uppon exiting, we restore
           the original settings, this wouldn't happen unless we also
           set HUPCL to origtermios. */
        if ( t->currtermios.c_cflag & HUPCL )
            t->origtermios.c_cflag |= HUPCL;
        else
            t->origtermios.c_cflag &= ~HUPCL;

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_set_raw (int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        /* BSD raw mode */
        cfmakeraw(&t->nexttermios);
        /* one byte at a time, no timer */
        t->nexttermios.c_cc[VMIN] = 1;
        t->nexttermios.c_cc[VTIME] = 0;

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_set_baudrate (int fd, int baudrate)
{
    int rval, r;
    struct term_s *t;
    speed_t spd;
    struct termios tio;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tio = t->nexttermios;
        spd = Bcode(baudrate);
        if ( spd != BNONE ) {
            r = cfsetospeed(&tio, spd);
            if ( r < 0 ) {
                term_errno = TERM_ESETOSPEED;
                rval = -1;
                break;
            }
            /* ispeed = 0, means same as ospeed (see POSIX) */
            cfsetispeed(&tio, B0);
        } else {
#ifdef USE_CUSTOM_BAUD
            r = cfsetospeed_custom(&tio, baudrate);
            if ( r < 0 ) {
                term_errno = TERM_ESETOSPEED;
                rval = -1;
                break;
            }
            /* ispeed = 0, means same as ospeed (see POSIX) */
            cfsetispeed(&tio, B0);
#else /* ! defined USE_CUSTOM_BAUD */
            term_errno = TERM_EBAUD;
            rval = -1;
            break;
#endif /* of USE_CUSTOM_BAUD */
        }

        t->nexttermios = tio;

    } while (0);

    return rval;
}

int
term_get_baudrate (int fd, int *ispeed)
{
    speed_t code;
    int ospeed;
    struct term_s *t;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            ospeed = -1;
            break;
        }

        if ( ispeed ) {
            code = cfgetispeed(&t->currtermios);
            *ispeed = Bspeed(code);
#ifdef USE_CUSTOM_BAUD
            if ( *ispeed < 0 ) {
                *ispeed = cfgetispeed_custom(&t->currtermios);
            }
#endif
        }
        code = cfgetospeed(&t->currtermios);
        ospeed = Bspeed(code);
        if ( ospeed < 0 ) {
#ifdef USE_CUSTOM_BAUD
            ospeed = cfgetospeed_custom(&t->currtermios);
            if ( ospeed < 0 ) {
                term_errno = TERM_EGETSPEED;
            }
#else
            term_errno = TERM_EGETSPEED;
#endif
        }

    } while (0);

    return ospeed;
}

/***************************************************************************/

int
term_set_parity (int fd, enum parity_e parity)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        switch (parity) {
        case P_EVEN:
            tiop->c_cflag &= ~(PARODD | CMSPAR);
            tiop->c_cflag |= PARENB;
            break;
        case P_ODD:
            tiop->c_cflag &= ~CMSPAR;
            tiop->c_cflag |= PARENB | PARODD;
            break;
        case P_MARK:
            tiop->c_cflag |= PARENB | PARODD | CMSPAR;
            break;
        case P_SPACE:
            tiop->c_cflag &= ~PARODD;
            tiop->c_cflag |= PARENB | CMSPAR;
            break;
        case P_NONE:
            tiop->c_cflag &= ~(PARENB | PARODD | CMSPAR);
            break;
        default:
            term_errno = TERM_EPARITY;
            rval = -1;
            break;
        }
        if ( rval < 0 ) break;

    } while (0);

    return rval;
}

enum parity_e
term_get_parity (int fd)
{
    tcflag_t flg;
    struct term_s *t;
    enum parity_e parity;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            parity = P_ERROR;
            break;
        }

        flg = t->currtermios.c_cflag;
        if ( ! (flg & PARENB) ) {
            parity = P_NONE;
        } else if ( flg & CMSPAR ) {
            parity = (flg & PARODD) ? P_MARK : P_SPACE;
        } else {
            parity = (flg & PARODD) ? P_ODD : P_EVEN;
        }

    } while (0);

    return parity;
}

/***************************************************************************/

int
term_set_databits (int fd, int databits)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        switch (databits) {
        case 5:
            tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS5;
            break;
        case 6:
            tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS6;
            break;
        case 7:
            tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS7;
            break;
        case 8:
            tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS8;
            break;
        default:
            term_errno = TERM_EDATABITS;
            rval = -1;
            break;
        }
        if ( rval < 0 ) break;

    } while (0);

    return rval;
}

int
term_get_databits (int fd)
{
    tcflag_t flg;
    struct term_s *t;
    int bits;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            bits = -1;
            break;
        }

        flg = t->currtermios.c_cflag & CSIZE;
        switch (flg) {
        case CS5:
            bits = 5;
            break;
        case CS6:
            bits = 6;
            break;
        case CS7:
            bits = 7;
            break;
        case CS8:
        default:
            bits = 8;
            break;
        }

    } while (0);

    return bits;
}

/***************************************************************************/

int
term_set_stopbits (int fd, int stopbits)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        switch (stopbits) {
        case 1:
            tiop->c_cflag &= ~CSTOPB;
            break;
        case 2:
            tiop->c_cflag |= CSTOPB;
            break;
        default:
            term_errno = TERM_ESTOPBITS;
            rval = -1;
            break;
        }
        if ( rval < 0 ) break;

    } while (0);

    return rval;
}

int
term_get_stopbits (int fd)
{
    struct term_s *t;
    int bits;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            bits = -1;
            break;
        }

        bits = (t->currtermios.c_cflag & CSTOPB) ? 2 : 1;

    } while (0);

    return bits;
}

/***************************************************************************/

int
term_set_flowcntrl (int fd, enum flowcntrl_e flowcntl)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        switch (flowcntl) {
        case FC_RTSCTS:
            tiop->c_cflag |= CRTSCTS;
            tiop->c_iflag &= ~(IXON | IXOFF | IXANY);
            break;
        case FC_XONXOFF:
            tiop->c_cflag &= ~(CRTSCTS);
            tiop->c_iflag |= IXON | IXOFF;
            break;
        case FC_NONE:
            tiop->c_cflag &= ~(CRTSCTS);
            tiop->c_iflag &= ~(IXON | IXOFF | IXANY);
            break;
        default:
            term_errno = TERM_EFLOW;
            rval = -1;
            break;
        }
        if ( rval < 0 ) break;

    } while (0);

    return rval;
}

enum flowcntrl_e
term_get_flowcntrl (int fd)
{
    struct term_s *t;
    enum flowcntrl_e flow;
    int rtscts, xoff, xon;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            flow = FC_ERROR;
            break;
        }

        rtscts = (t->currtermios.c_cflag & CRTSCTS) ? 1 : 0;
        xoff = (t->currtermios.c_iflag & IXOFF) ? 1 : 0;
        xon = (t->currtermios.c_iflag & (IXON | IXANY)) ? 1 : 0;

        if ( rtscts && ! xoff && ! xon ) {
            flow = FC_RTSCTS;
        } else if ( ! rtscts && xoff && xon ) {
            flow = FC_XONXOFF;
        } else if ( ! rtscts && ! xoff && ! xon ) {
            flow = FC_NONE;
        } else {
            flow = FC_OTHER;
        }

    } while (0);

    return flow;
}

/***************************************************************************/

int
term_set_local(int fd, int local)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        if ( local )
            tiop->c_cflag |= CLOCAL;
        else
            tiop->c_cflag &= ~CLOCAL;

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_set_hupcl (int fd, int on)
{
    int rval;
    struct term_s *t;
    struct termios *tiop;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tiop = &t->nexttermios;

        if ( on )
            tiop->c_cflag |= HUPCL;
        else
            tiop->c_cflag &= ~HUPCL;

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_set(int fd,
         int raw,
         int baud,
         enum parity_e parity,
         int databits, int stopbits,
         enum flowcntrl_e fc,
         int local, int hup_close)
{
    int rval, r;
    struct term_s *t;
    struct termios tio;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        tio = t->nexttermios;

        do { /* dummy */

            if (raw) {
                r = term_set_raw(fd);
                if ( r < 0 ) { rval = -1; break; }
            }

            r = term_set_baudrate(fd, baud);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_parity(fd, parity);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_databits(fd, databits);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_stopbits(fd, stopbits);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_flowcntrl(fd, fc);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_local(fd, local);
            if ( r < 0 ) { rval = -1; break; }

            r = term_set_hupcl(fd, hup_close);
            if ( r < 0 ) { rval = -1; break; }

        } while (0);

        if ( rval < 0 ) {
            /* revert to previous settings */
            t->nexttermios = tio;
        }

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_pulse_dtr (int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int opins = TIOCM_DTR;

            r = t->ops->modem_bic(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_EDTRDOWN;
                rval = -1;
                break;
            }

            sleep(1);

            r = t->ops->modem_bis(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_EDTRUP;
                rval = -1;
                break;
            }
        }
#else
        {
            struct termios tio, tioold;

            r = t->ops->tcgetattr(t, &tio);
            if ( r < 0 ) {
                term_errno = TERM_EGETATTR;
                rval = -1;
                break;
            }

            tioold = tio;

            /* ospeed = 0, means hangup (see POSIX) */
            cfsetospeed(&tio, B0);
            r = t->ops->tcsetattr(t, TCSANOW, &tio);
            if ( r < 0 ) {
                term_errno = TERM_ESETATTR;
                rval = -1;
                break;
            }

            sleep(1);

            r = t->ops->tcsetattr(t, TCSANOW, &tioold);
            if ( r < 0 ) {
                t->currtermios = tio;
                term_errno = TERM_ESETATTR;
                rval = -1;
                break;
            }
        }
#endif /* of USE_IOCTL */

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_raise_dtr(int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int r, opins = TIOCM_DTR;

            r = t->ops->modem_bis(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_EDTRUP;
                rval = -1;
                break;
            }
        }
#else
        term_errno = TERM_EDTRUP;
        rval = -1;
#endif /* of USE_IOCTL */
    } while (0);

    return rval;
}

/***************************************************************************/


int
term_lower_dtr(int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int r, opins = TIOCM_DTR;

            r = t->ops->modem_bic(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_EDTRDOWN;
                rval = -1;
                break;
            }
        }
#else
        term_errno = TERM_EDTRDOWN;
        rval = -1;
#endif /* of USE_IOCTL */
    } while (0);

    return rval;
}

/***************************************************************************/

int
term_raise_rts(int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int r;
            int opins = TIOCM_RTS;

            r = t->ops->modem_bis(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_ERTSUP;
                rval = -1;
                break;
            }
        }
#else
        term_errno = TERM_ERTSUP;
        rval = -1;
#endif /* of USE_IOCTL */
    } while (0);

    return rval;
}

/***************************************************************************/

int
term_lower_rts(int fd)
{
    int rval;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int r;
            int opins = TIOCM_RTS;

            r = t->ops->modem_bic(t, &opins);
            if ( r < 0 ) {
                term_errno = TERM_ERTSDOWN;
                rval = -1;
                break;
            }
        }
#else
        term_errno = TERM_ERTSDOWN;
        rval = -1;
#endif /* of USE_IOCTL */
    } while (0);

    return rval;
}


/***************************************************************************/

int
term_get_mctl (int fd)
{
    int mctl;
    struct term_s *t;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            mctl = -1;
            break;
        }

#ifdef USE_IOCTL
        {
            int r, pmctl;

            r = t->ops->modem_get(t, &pmctl);
            if (r < 0) {
                mctl = -1;
                break;
            }
            mctl = 0;
            if (pmctl & TIOCM_DTR) mctl |= MCTL_DTR;
            if (pmctl & TIOCM_DSR) mctl |= MCTL_DSR;
            if (pmctl & TIOCM_CD) mctl |= MCTL_DCD;
            if (pmctl & TIOCM_RTS) mctl |= MCTL_RTS;
            if (pmctl & TIOCM_CTS) mctl |= MCTL_CTS;
            if (pmctl & TIOCM_RI) mctl |= MCTL_RI;
        }
#else
        mctl = MCTL_UNAVAIL;
#endif /* of USE_IOCTL */
    } while(0);

    return mctl;
}

int
term_drain(int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        do {
            r = t->ops->drain(t);
        } while ( r < 0 && errno == EINTR);
        if ( r < 0 ) {
            term_errno = TERM_EDRAIN;
            rval = -1;
            break;
        }
        /* Give some time to the UART to transmit everything. Some
           systems and / or drivers corrupt the last character(s) if
           the port is immediately reconfigured, even after a
           drain. (I guess, drain does not wait for everything to
           actually be transitted on the wire). */
        if ( DRAIN_DELAY ) usleep(DRAIN_DELAY);

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_fake_flush(int fd)
{
    struct termios tio;
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        /* Get current termios */
        r = t->ops->tcgetattr(t, &tio);
        if ( r < 0 ) {
            term_errno = TERM_EGETATTR;
            rval = -1;
            break;
        }
        t->currtermios = tio;
        /* Set flow-control to none */
        tio.c_cflag &= ~(CRTSCTS);
        tio.c_iflag &= ~(IXON | IXOFF | IXANY);
        /* Apply termios */
        r = t->ops->tcsetattr(t, TCSANOW, &tio);
        if ( r < 0 ) {
            term_errno = TERM_ESETATTR;
            rval = -1;
            break;
        }
        /* Wait for output to drain. Without flow-control this should
           complete in finite time. */
        r = t->ops->drain(t);
        if ( r < 0 ) {
            term_errno = TERM_EDRAIN;
            rval = -1;
            break;
        }
        /* see comment in term_drain */
        if ( DRAIN_DELAY ) usleep(DRAIN_DELAY);
        /* Reset flow-control to original setting. */
        r = t->ops->tcsetattr(t, TCSANOW, &t->currtermios);
        if ( r < 0 ) {
            term_errno = TERM_ESETATTR;
            rval = -1;
            break;
        }

    } while (0);

    return rval;
}

int
term_flush(int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->flush(t, TCIOFLUSH);
        if ( r < 0 ) {
            term_errno = TERM_EFLUSH;
            rval = -1;
            break;
        }

    } while (0);

    return rval;
}

/***************************************************************************/

int
term_break(int fd)
{
    int rval, r;
    struct term_s *t;

    rval = 0;

    do { /* dummy */

        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->send_break(t);
        if ( r < 0 ) {
            term_errno = TERM_EBREAK;
            rval = -1;
            break;
        }

    } while (0);

    return rval;
}

/**************************************************************************/

int
term_read (int fd, void *buf, unsigned int bufsz)
{
    int rval;
    struct term_s *t;

    do { /* dummy */
        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }
        rval = t->ops->read(t, buf, bufsz);
    } while (0);

    return rval;
}

int
term_write (int fd, const void *buf, unsigned int bufsz)
{
    int rval;
    struct term_s *t;

    do { /* dummy */
        t = term_find(fd);
        if ( ! t ) {
            rval = -1;
            break;
        }
        rval = t->ops->write(t, buf, bufsz);
    } while (0);

    return rval;
}


/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
