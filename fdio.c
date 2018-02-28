/* vi: set sw=4 ts=4:
 *
 * fdio.c
 *
 * Functions for doing I/O on file descriptors.
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

/**********************************************************************/

struct timeval *
msec2tv (struct timeval *tv, long ms)
{
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;

    return tv;
}

ssize_t
writen_ni(int fd, const void *buff, size_t n)
{
    size_t nl;
    ssize_t nw;
    const char *p;
    fd_set wrset;
    int r;

    p = buff;
    nl = n;
    while (nl > 0) {
        do {
            nw = write(fd, p, nl);
        } while ( nw < 0 && errno == EINTR );
        if ( nw <= 0 ) {
            if ( nw < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) ) {
                FD_ZERO(&wrset); FD_SET(fd, &wrset);
                do {
                    r = select(fd + 1, 0, &wrset, 0, NULL);
                } while ( r < 0 && errno == EINTR);
                if ( r < 0 ) break;
                continue;
            }
            break;
        }
        nl -= nw;
        p += nw;
    }

    return n - nl;
}

# if 0

ssize_t
writento_ni(int fd, const void *buff, size_t n,
            int absolute, struct timeval *tv_tmo)
{
    size_t nl;
    ssize_t nw;
    const char *p;
    struct timeval tv_abs, now;
    fd_set wrset;
    int r;

    if ( tv_tmo ) {
        if ( ! absolute ) {
            gettimeofday(&now, 0);
            timeradd(&now, tv_tmo, &tv_abs);
        } else tv_abs = *tv_tmo;
    }

    p = buff;
    nl = n;
    while (nl > 0) {
        if ( tv_tmo ) {
            gettimeofday(&now, 0);
            if ( ! timercmp(&now, &tv_abs, <) ) {
                errno = ETIMEDOUT;
                break;
            }
            timersub(&tv_abs, &now, tv_tmo);
        }

        FD_ZERO(&wrset);
        FD_SET(fd, &wrset);
        do {
            r = select(fd + 1, 0, &wrset, 0, tv_tmo);
        } while ( r < 0 && errno == EINTR );
        if ( r < 0 ) break;
        if ( r == 0 ) { errno = ETIMEDOUT; break; }

        do {
            nw = write(fd, p, nl);
        } while ( nw < 0 && errno == EINTR );
        if ( nw <= 0 ) {
            if ( r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) )
                continue;
            break;
        }
        nl -= nw;
        p += nw;
    }

    return n - nl;
}

#endif

int
fd_vprintf (int fd, const char *format, va_list ap)
{
    char buf[256];
    int len;

    len = vsnprintf(buf, sizeof(buf), format, ap);
    buf[sizeof(buf) - 1] = '\0';

    return writen_ni(fd, buf, len);
}

int
fd_printf (int fd, const char *format, ...)
{
    va_list args;
    int len;

    va_start(args, format);
    len = fd_vprintf(fd, format, args);
    va_end(args);

    return len;
}

/**********************************************************************/

#ifndef LINENOISE

static int
cput(int fd, char c)
{
    return write(fd, &c, 1);
}

static int
cdel (int fd)
{
    const char del[] = "\b \b";
    return write(fd, del, sizeof(del) - 1);
}

static int
xput (int fd, unsigned char c)
{
    const char hex[] = "0123456789abcdef";
    char b[4];

    b[0] = '\\'; b[1] = 'x'; b[2] = hex[c >> 4]; b[3] = hex[c & 0x0f];
    return write(fd, b, sizeof(b));
}

static int
xdel (int fd)
{
    const char del[] = "\b\b\b\b    \b\b\b\b";
    return write(fd, del, sizeof(del) - 1);
}

int
fd_readline (int fdi, int fdo, char *b, int bsz)
{
    int r;
    unsigned char c;
    unsigned char *bp, *bpe;

    bp = (unsigned char *)b;
    bpe = (unsigned char *)b + bsz - 1;

    while (1) {
        r = read(fdi, &c, 1);
        if ( r <= 0 ) { r = -1; goto out; }

        switch (c) {
        case '\b':
        case '\x7f':
            if ( bp > (unsigned char *)b ) {
                bp--;
                if ( isprint(*bp) )
                    cdel(fdo);
                else
                    xdel(fdo);
            } else {
                cput(fdo, '\x07');
            }
            break;
        case '\x03': /* CTRL-c */
            r = -1;
            errno = EINTR;
            goto out;
        case '\r':
            *bp = '\0';
            r = bp - (unsigned char *)b;
            goto out;
        default:
            if ( bp < bpe ) {
                *bp++ = c;
                if ( isprint(c) )
                    cput(fdo, c);
                else
                    xput(fdo, c);
            } else {
                cput(fdo, '\x07');
            }
            break;
        }
    }

out:
    return r;
}

#endif /* of LINENOISE */

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
