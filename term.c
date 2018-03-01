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

#include "custbaud.h"
#ifdef USE_CUSTOM_BAUD
#include CUSTOM_BAUD_HEAD
#endif

#include "term.h"
#include "termint.h"

/***************************************************************************/

static int term_initted;

static struct term_s term[MAX_TERMS];

/* defined and initialized in ttylocal.c */
extern struct term_ops ttylocal_term_ops;

/***************************************************************************/

int term_errno;

static const char * const term_err_str[] = {
    /* Internal errors (no errno) */
    [TERM_EOK]        = "No error",
    [TERM_EMEM]       = "Memory allocation failed",
    [TERM_EUNSUP]     = "Operation not supported",
    [TERM_ENOINIT]    = "Framework is uninitialized",
    [TERM_EFULL]      = "Framework is full",
    [TERM_ENOTFOUND]  = "Filedes not in the framework",
    [TERM_EEXISTS]    = "Filedes already in the framework",
    [TERM_EATEXIT]    = "Cannot install atexit handler",
    [TERM_EISATTY]    = "Filedes is not a tty",
    [TERM_EBAUD]      = "Invalid baud rate",
    [TERM_EPARITY]    = "Invalid parity mode",
    [TERM_EDATABITS]  = "Invalid number of databits",
    [TERM_ESTOPBITS]  = "Invalid number of stopbits",
    [TERM_EFLOW]      = "Invalid flowcontrol mode",
    [TERM_ETIMEDOUT]  = "Operation timed-out",
    [TERM_ERDZERO]    = "Read zero bytes",

    /* System errors (check errno for more) */
    [TERM_EGETATTR]   = "Get attributes error",
    [TERM_ESETATTR]   = "Set attributes error",
    [TERM_EFLUSH]     = "Flush error",
    [TERM_EDRAIN]     = "Drain error",
    [TERM_EBREAK]     = "Break error",
    [TERM_ESETOSPEED] = "Set output speed error",
    [TERM_ESETISPEED] = "Set input speed error",
    [TERM_EGETSPEED]  = "Get speed error",
    [TERM_EGETMCTL]   = "Get mctl status error",
    [TERM_ESETMCTL]   = "Set mctl status error",
    [TERM_EINPUT]     = "Input error",
    [TERM_EOUTPUT]    = "Output error",
    [TERM_ESELECT]    = "Select error",
};

static char term_err_buff[1024];

int
term_esys (void)
{
    return ( term_errno > TERM_EINTEND && term_errno < TERM_EEND ) ? errno : 0;
}

const char *
term_strerror (int terrnum, int errnum)
{
    const char *rval;

    if ( term_errno > TERM_EINTEND && term_errno < TERM_EEND ) {
        snprintf(term_err_buff, sizeof(term_err_buff),
                 "%s: %s", term_err_str[terrnum], strerror(errnum));
        rval = term_err_buff;
    } else if ( term_errno >=0 && term_errno < TERM_EINTEND ) {
        snprintf(term_err_buff, sizeof(term_err_buff),
                 "%s", term_err_str[terrnum]);
        rval = term_err_buff;
    } else
        rval = NULL;

    return rval;
}

int
term_perror (const char *prefix)
{
    return fprintf(stderr, "%s %s\n",
                   prefix, term_strerror(term_errno, errno));
}

/***************************************************************************/

/* parity modes names */
const char *parity_str[] = {
    [P_NONE] = "none",
    [P_EVEN] = "even",
    [P_ODD] = "odd",
    [P_MARK] = "mark",
    [P_SPACE] = "space",
    [P_ERROR] = "invalid parity mode",
};

/* flow control modes names */
const char *flow_str[] = {
    [FC_NONE] = "none",
    [FC_RTSCTS] = "RTS/CTS",
    [FC_XONXOFF] = "xon/xoff",
    [FC_OTHER] = "other",
    [FC_ERROR] = "invalid flow control mode",
};

/**********************************************************************/

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

int
Bspeed_nearest(int speed)
{
    int i, mid;

    if ( speed <= baud_table[1].speed )
        return baud_table[1].speed;

    for (i = 2; i < BAUD_TABLE_SZ; i++) {
        if ( speed <= baud_table[i].speed ) {
            mid = ( baud_table[i].speed + baud_table[i-1].speed ) / 2;
            if ( speed >= mid )
                return baud_table[i].speed;
            else
                return baud_table[i-1].speed;
        }
    }

    return baud_table[BAUD_TABLE_SZ -1].speed;
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
    if ( use_custom_baud() )
        return (baud >= 0);
    else
        return (Bcode(baud) != BNONE) ? 1 : 0;
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
            if (t->ops->drain) t->ops->drain(t);
            if (t->ops->flush) t->ops->flush(t, TCIFLUSH);
            r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
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
                if (t->ops->flush) t->ops->flush(t, TCIOFLUSH);
                r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
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
            ops = &ttylocal_term_ops;

        t = term_new(fd, name, ops);
        if ( ! t ) {
            rval = -1;
            break;
        }

        r = t->ops->tcgetattr(t, &t->origtermios);
        if ( r < 0 ) {
            term_free(t->fd);
            rval = -1;
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
            rval = -1;
            t->fd = oldfd;
            break;
        }
        r = t->ops->tcgetattr(t, &t->currtermios);
        if ( r < 0 ) {
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

        if ( t->ops->flush ) {
            r = t->ops->flush(t, TCIOFLUSH);
            if ( r < 0 ) {
                rval = -1;
                break;
            }
        }
        r = t->ops->tcsetattr(t, TCSANOW, &t->origtermios);
        if ( r < 0 ) {
            rval = -1;
            break;
        }
        r = t->ops->tcgetattr(t, &t->currtermios);
        if ( r < 0 ) {
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
            rval = -1;
            break;
        }
        r = t->ops->tcgetattr(t, &t->nexttermios);
        if ( r < 0 ) {
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
tios_set_baudrate (struct termios *tios, int baudrate)
{
    int rval, r;
    speed_t spd;
    struct termios ttios;

    rval = 0;
    ttios = *tios;
    do { /* dummy */
        spd = Bcode(baudrate);
        if ( spd != BNONE ) {
            r = cfsetospeed(&ttios, spd);
            if ( r < 0 ) {
                term_errno = TERM_ESETOSPEED;
                rval = -1;
                break;
            }
            /* ispeed = 0, means same as ospeed */
            cfsetispeed(&ttios, B0);
        } else {
            if ( ! use_custom_baud() ) {
                term_errno = TERM_EBAUD;
                rval = -1;
                break;
            }
            r = cfsetospeed_custom(&ttios, baudrate);
            if ( r < 0 ) {
                term_errno = TERM_ESETOSPEED;
                rval = -1;
                break;
            }
            /* ispeed = 0, means same as ospeed (see POSIX) */
            cfsetispeed(&ttios, B0);
        }
        *tios = ttios;
    } while (0);

    return rval;
}

void
tios_set_baudrate_always(struct termios *tios, int baudrate)
{
    int r;

    r = tios_set_baudrate(tios, baudrate);
    if ( r < 0 ) {
        if ( term_errno == TERM_EBAUD ) {
            /* find the nearest, non-custom speed and try with this */
            baudrate = Bspeed_nearest(baudrate);
            r = tios_set_baudrate(tios, baudrate);
            if ( r >= 0 ) return;
        }
        /* last resort, set B0 */
        tios_set_baudrate(tios, 0);
    }
}

int
term_set_baudrate (int fd, int baudrate)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_set_baudrate(&t->nexttermios, baudrate);
}

int
tios_get_baudrate(const struct termios *tios, int *ispeed)
{
    speed_t code;
    int ospeed;

    if ( ispeed ) {
        code = cfgetispeed(tios);
        *ispeed = Bspeed(code);
        if ( use_custom_baud() ) {
            if ( *ispeed < 0 ) {
                *ispeed = cfgetispeed_custom(tios);
            }
        }
    }
    code = cfgetospeed(tios);
    ospeed = Bspeed(code);
    if ( ospeed < 0 ) {
        if ( ! use_custom_baud() ) {
            term_errno = TERM_EGETSPEED;
            return ospeed;
        }
        ospeed = cfgetospeed_custom(tios);
        if ( ospeed < 0 ) {
            term_errno = TERM_EGETSPEED;
            return ospeed;
        }
    }

    return ospeed;
}

int
term_get_baudrate (int fd, int *ispeed)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_get_baudrate(&t->currtermios, ispeed);
}

/***************************************************************************/

int
tios_set_parity (struct termios *tios, enum parity_e parity)
{
    int rval = 0;

    switch (parity) {
    case P_EVEN:
        tios->c_cflag &= ~(PARODD | CMSPAR);
        tios->c_cflag |= PARENB;
        break;
    case P_ODD:
        tios->c_cflag &= ~CMSPAR;
        tios->c_cflag |= PARENB | PARODD;
        break;
    case P_MARK:
        tios->c_cflag |= PARENB | PARODD | CMSPAR;
        break;
    case P_SPACE:
        tios->c_cflag &= ~PARODD;
        tios->c_cflag |= PARENB | CMSPAR;
        break;
    case P_NONE:
        tios->c_cflag &= ~(PARENB | PARODD | CMSPAR);
        break;
    default:
        term_errno = TERM_EPARITY;
        rval = -1;
        break;
    }

    return rval;
}

int
term_set_parity (int fd, enum parity_e parity)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_set_parity(&t->nexttermios, parity);
}

enum parity_e
tios_get_parity(const struct termios *tios)
{
    enum parity_e parity;

    if ( ! (tios->c_cflag & PARENB) ) {
        parity = P_NONE;
    } else if ( tios->c_cflag & CMSPAR ) {
        parity = (tios->c_cflag & PARODD) ? P_MARK : P_SPACE;
    } else {
        parity = (tios->c_cflag & PARODD) ? P_ODD : P_EVEN;
    }
    return parity;
}

enum parity_e
term_get_parity (int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return P_ERROR;

    return tios_get_parity(&t->currtermios);
}

/***************************************************************************/

int
tios_set_databits(struct termios *tios, int databits)
{
    int rval = 0;

    switch (databits) {
    case 5:
        tios->c_cflag = (tios->c_cflag & ~CSIZE) | CS5;
        break;
    case 6:
        tios->c_cflag = (tios->c_cflag & ~CSIZE) | CS6;
        break;
    case 7:
        tios->c_cflag = (tios->c_cflag & ~CSIZE) | CS7;
        break;
    case 8:
        tios->c_cflag = (tios->c_cflag & ~CSIZE) | CS8;
        break;
    default:
        term_errno = TERM_EDATABITS;
        rval = -1;
        break;
    }
    return rval;
}

int
term_set_databits (int fd, int databits)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_set_databits(&t->nexttermios, databits);
}

int
tios_get_databits(const struct termios *tios)
{
    int bits;

    switch (tios->c_cflag & CSIZE) {
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
    return bits;
}

int
term_get_databits (int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_get_databits(&t->currtermios);
}

/***************************************************************************/

int
tios_set_stopbits (struct termios *tios, int stopbits)
{
    int rval = 0;
    switch (stopbits) {
    case 1:
        tios->c_cflag &= ~CSTOPB;
        break;
    case 2:
        tios->c_cflag |= CSTOPB;
        break;
    default:
        term_errno = TERM_ESTOPBITS;
        rval = -1;
        break;
    }
    return rval;
}

int
term_set_stopbits (int fd, int stopbits)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_set_stopbits(&t->nexttermios, stopbits);
}

int
tios_get_stopbits (const struct termios *tios)
{
    return (tios->c_cflag & CSTOPB) ? 2 : 1;
}

int
term_get_stopbits (int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_get_stopbits(&t->currtermios);
}

/***************************************************************************/

int
tios_set_flowcntrl(struct termios *tios, enum flowcntrl_e flowcntl)
{
    int rval = 0;

    switch (flowcntl) {
    case FC_RTSCTS:
            tios->c_cflag |= CRTSCTS;
            tios->c_iflag &= ~(IXON | IXOFF | IXANY);
            break;
    case FC_XONXOFF:
        tios->c_cflag &= ~(CRTSCTS);
        tios->c_iflag |= IXON | IXOFF;
        break;
    case FC_NONE:
        tios->c_cflag &= ~(CRTSCTS);
        tios->c_iflag &= ~(IXON | IXOFF | IXANY);
        break;
    default:
        term_errno = TERM_EFLOW;
        rval = -1;
        break;
    }
    return rval;
}

int
term_set_flowcntrl (int fd, enum flowcntrl_e flowcntl)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return tios_set_flowcntrl(&t->nexttermios, flowcntl);
}

enum flowcntrl_e
tios_get_flowcntrl(const struct termios *tios)
{
    enum flowcntrl_e flow;
    int rtscts, xoff, xon;

    rtscts = (tios->c_cflag & CRTSCTS) ? 1 : 0;
    xoff = (tios->c_iflag & IXOFF) ? 1 : 0;
    xon = (tios->c_iflag & (IXON | IXANY)) ? 1 : 0;

    if ( rtscts && ! xoff && ! xon ) {
        flow = FC_RTSCTS;
    } else if ( ! rtscts && xoff && xon ) {
        flow = FC_XONXOFF;
    } else if ( ! rtscts && ! xoff && ! xon ) {
        flow = FC_NONE;
    } else {
        flow = FC_OTHER;
    }

    return flow;
}

enum flowcntrl_e
term_get_flowcntrl (int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return FC_ERROR;

    return tios_get_flowcntrl(&t->currtermios);
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

        if ( t->ops->modem_bic && t->ops->modem_bis )  {
            int opins = MCTL_DTR;

            r = t->ops->modem_bic(t, &opins);
            if ( r < 0 ) {
                rval = -1;
                break;
            }

            sleep(1);

            r = t->ops->modem_bis(t, &opins);
            if ( r < 0 ) {
                rval = -1;
                break;
            }
        } else {
            struct termios tio, tioold;

            r = t->ops->tcgetattr(t, &tio);
            if ( r < 0 ) {
                rval = -1;
                break;
            }

            tioold = tio;

            /* ospeed = 0, means hangup (see POSIX) */
            cfsetospeed(&tio, B0);
            r = t->ops->tcsetattr(t, TCSANOW, &tio);
            if ( r < 0 ) {
                rval = -1;
                break;
            }

            sleep(1);

            r = t->ops->tcsetattr(t, TCSANOW, &tioold);
            if ( r < 0 ) {
                t->currtermios = tio;
                rval = -1;
                break;
            }
        }
    } while (0);

    return rval;
}

/***************************************************************************/

static int
set_pins (int fd, int raise, int pins)
{
    struct term_s *t;
    int (*rol)(struct term_s *, const int *);

    t = term_find(fd);
    if ( ! t ) return -1;

    rol = raise ? t->ops->modem_bis : t->ops->modem_bic;
    if ( ! rol ) {
        term_errno = TERM_EUNSUP; return -1;
    }

    return rol(t, &pins);
}

int term_raise_dtr(int fd) { return set_pins(fd, 1, MCTL_DTR); }
int term_lower_dtr(int fd) { return set_pins(fd, 0, MCTL_DTR); }
int term_raise_rts(int fd) { return set_pins(fd, 1, MCTL_RTS); }
int term_lower_rts(int fd) { return set_pins(fd, 0, MCTL_RTS); }

/***************************************************************************/

int
term_get_mctl (int fd)
{
    int r, mctl;
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    if ( ! t->ops->modem_get ) {
        return MCTL_UNAVAIL;
    }

    r = t->ops->modem_get(t, &mctl);
    if (r < 0) return -1;

    return mctl;
}

/***************************************************************************/

int
term_drain(int fd)
{
    struct term_s *t;
        t = term_find(fd);
        if ( ! t ) return -1;

        if ( ! t->ops->drain ) {
            term_errno = TERM_EUNSUP; return -1;
        }
        return t->ops->drain(t);
}

/***************************************************************************/

int
term_fake_flush(int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    if ( ! t->ops->fake_flush ) {
        term_errno = TERM_EUNSUP; return -1;
    }
    return t->ops->fake_flush(t);
}

int
term_flush(int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    if ( ! t->ops->flush ) {
        term_errno = TERM_EUNSUP; return -1;
    }
    return t->ops->flush(t, TCIOFLUSH);
}

/***************************************************************************/

int
term_break(int fd)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    if ( ! t->ops->send_break ) {
        term_errno = TERM_EUNSUP; return -1;
    }
    return t->ops->send_break(t);
}

/**************************************************************************/

int
term_read (int fd, void *buf, unsigned int bufsz)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return t->ops->read(t, buf, bufsz);
}

int
term_write (int fd, const void *buf, unsigned int bufsz)
{
    struct term_s *t;

    t = term_find(fd);
    if ( ! t ) return -1;

    return t->ops->write(t, buf, bufsz);
}


/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
