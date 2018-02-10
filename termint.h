#ifndef TERMINT_H
#define TERMINT_H

#include <termios.h>

struct term_s {
    int fd;
    const struct term_ops *ops;
    struct termios origtermios;
    struct termios currtermios;
    struct termios nexttermios;
};

/* Operations on a term */
struct term_ops {
    int (*tcgetattr)(struct term_s *t, struct termios *termios_out);
    int (*tcsetattr)(struct term_s *t, int when, const struct termios *termios);
    int (*modem_get)(struct term_s *t, int *modem_out);
    int (*modem_bis)(struct term_s *t, const int *modem);
    int (*modem_bic)(struct term_s *t, const int *modem);
    int (*send_break)(struct term_s *t);
    int (*flush)(struct term_s *t, int selector);
    int (*read)(struct term_s *t, void *buf, unsigned bufsz);
    int (*write)(struct term_s *t, const void *buf, unsigned bufsz);
};

#endif /* of TERMINT_H */
