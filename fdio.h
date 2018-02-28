/* vi: set sw=4 ts=4:
 *
 * fdio.h
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

#ifndef FDIO_H

struct timeval *msec2tv (struct timeval *tv, long ms);

/* Write exactly "n" bytes from "buff" to "fd". Works with blocking
   and non-blocking fds. Returns number of bytes written; if != "n",
   sets errno. */
ssize_t writen_ni(int fd, const void *buff, size_t n);

#if 0
/* Write exactly "n" bytes from "buff" to "fd", with an optional
   (relative or absolute) timeout "tv_tmo".  Works with blocking and
   non-blocking fds. Returns number of bytes written; if != "n", sets
   errno. */
ssize_t writento_ni(int fd, const void *buff, size_t n,
                    int absolute, struct timeval *tv_tmo);
#endif

int fd_vprintf (int fd, const char *format, va_list ap);

int fd_printf (int fd, const char *format, ...);

#ifndef LINENOISE

int fd_readline (int fdi, int fdo, char *b, int bsz);

#endif

#endif /* of FDIO_H */

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
