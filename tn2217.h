/* vi: set sw=4 ts=4:
 *
 * tn2217.h
 *
 * TELNET and COM-PORT (RFC2217) remote terminal protocol.
 *
 * by David Leonard (https://github.com/dleonard0)
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

#ifndef TN2217_H
#define TN2217_H

/* Open a socket to a telnet service.
 * The port string will be of the form "<hostname>[,<port>]".
 * If the port is omitted, uses "telnet" from /etc/servcies.
 * The tn2217_ops should be used to interact with the returned fd.
 * Returns -1 on failure, and prints an error message to stderr.  */
int tn2217_open(const char *port);

/* Close a socket to a telnet service.  If "drain" is zero, the socket
 * is just close(2)ed and the return-value of close(2) is returned. If
 * "drain" is non-zero, the write-direction of the socket is first
 * shutdown, and then data are read (and discarded) from the socket,
 * until the remote end closes it (read(2) returns zero). Returns zero
 * on success, a negative on error. */
int tn2217_close(int fd, int drain);

struct term_ops;
extern const struct term_ops tn2217_ops;

#endif /* of TN2217_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
