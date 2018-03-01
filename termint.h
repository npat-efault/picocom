/* vi: set sw=4 ts=4:
 *
 * termint.h
 *
 * General purpose terminal handling library. Internal declarations.
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

#ifndef TERMINT_H
#define TERMINT_H

#include <termios.h>
#include "term.h"

struct term_s {
    /* Read-only fields */
    int fd;
    char *name;
    const struct term_ops *ops;

    struct termios origtermios;
    struct termios currtermios;
    struct termios nexttermios;
    void *priv;
};

/* Operations on a term */
struct term_ops {
    int (*init)(struct term_s *t);
    void (*fini)(struct term_s *t);
    int (*tcgetattr)(struct term_s *t, struct termios *termios_out);
    int (*tcsetattr)(struct term_s *t, int when, const struct termios *termios);
    int (*modem_get)(struct term_s *t, int *modem_out);
    int (*modem_bis)(struct term_s *t, const int *modem);
    int (*modem_bic)(struct term_s *t, const int *modem);
    int (*send_break)(struct term_s *t);
    int (*flush)(struct term_s *t, int selector);
    int (*fake_flush)(struct term_s *t);
    int (*drain)(struct term_s *t);
    int (*read)(struct term_s *t, void *buf, unsigned bufsz);
    int (*write)(struct term_s *t, const void *buf, unsigned bufsz);
};

enum flowcntrl_e tios_get_flowcntrl(const struct termios *tios);
int tios_set_flowcntrl(struct termios *tios, enum flowcntrl_e flowcntl);
int tios_get_baudrate(const struct termios *tios, int *ispeed);
int tios_set_baudrate (struct termios *tios, int baudrate);
void tios_set_baudrate_always(struct termios *tios, int baudrate);
int tios_set_parity (struct termios *tios, enum parity_e parity);
enum parity_e tios_get_parity(const struct termios *tios);
int tios_set_databits(struct termios *tios, int databits);
int tios_get_databits(const struct termios *tios);
int tios_set_stopbits (struct termios *tios, int stopbits);
int tios_get_stopbits (const struct termios *tios);




#endif /* of TERMINT_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
