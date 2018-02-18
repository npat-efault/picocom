/* vi: set sw=4 ts=4:
 *
 * tn2217.c
 *
 * TELNET and COM-PORT (RFC2217) remote terminal protocol.
 *
 * Provides a network virtual terminal over a TELNET TCP connection.
 * Optionally supports the TELNET COM PORT (RFC2217) option which
 * provides control over baud rate, parity, data bits and modem
 * control lines at a com port server.
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "fdio.h"
#include "termint.h"
#include "tncomport.h"
#include "tn2217.h"

#include <sys/ioctl.h>
#include <arpa/telnet.h>

#if 0
# define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
# define DEBUG(...) /* nothing */
#endif

/* c_cflags mask used for termios parity */
#define PARMASK (PARENB | PARODD)

/* We'll ask the remote end to use this modem state mask */
#define MODEMSTATE_MASK (COMPORT_MODEM_CD | \
                         COMPORT_MODEM_RI | \
                         COMPORT_MODEM_DSR | \
                         COMPORT_MODEM_CTS )

/* RFC1143 Q method for TELNET option tracking. Thanks, djb!
 *
 * TELNET options are enabled independently at each side, and the Q method
 * is a robust way to track peer/local option state without entering loops.
 * The initial states are NO (disabled) for all options at each sides.
 *
 *   tn2217_do(), tn2217_dont(), tn2217_will(), tn2217_wont()
 *      - Called to drive the Q state towards the wanted direction.
 *      - These functions directly writes messages to the telnet socket
 *
 *   tn2217_recv_opt()
 *      - Called on receipt of DO/DONT/WILL/WONT, and update the Q state
 *        and reply with an answer on the telnet socket.
 *      - Calls tn2217_remote_should(), tn2217_local_should() to
 *        know how to answer options not explicity driven by tn2217_do() etc.
 *      - When an opt becomes stable, calls tn2217_check_options_changed().
 */
struct q_option {
    unsigned int us : 2,
                 him: 2,
# define            NO       0  /* Agreed disable */
# define            YES      1  /* Agreed enable */
# define            WANTYES  2  /* We sent WILL or DO */
# define            WANTNO   3  /* We sent WONT or DONT */
                 usq : 1,
                 himq : 1;
# define            EMPTY    0  /* queue is empty */
# define            OPPOSITE 1  /* queue contains an opposition */
};


/* TELNET protocol state. */
struct tn2217_state {
    struct q_option opt[256];   /* Negotiated TELNET options */

    struct termios termios;     /* Predicted remote com port geometry */
    int modem;                  /* Predicted remote com port signals */

    unsigned char cmdbuf[32];   /* IAC command accumulator */
    unsigned char cmdbuflen;
    unsigned int cmdiac : 1;    /* 1 iff last cmdbuf ch is incomplete IAC */

    /* When COM-PORT option is fully negotiated, can_comport
     * is set to true. This also means that any deferred termios/modem
     * settings will be triggered. */
    unsigned int can_comport : 1,  /* WILL COMPORT was received */
                 set_termios : 1,  /* tcsetattr called before can_comport */
                 set_modem : 1;    /* modem_bis/c called before can_comport */

};


static int tn2217_remote_should(struct term_s *t, unsigned char opt);
static int tn2217_local_should(struct term_s *t, unsigned char opt);
static int tn2217_write(struct term_s *t, const void *buf, unsigned bufsz);
static void tn2217_recv_comport_cmd(struct term_s *t, unsigned char cmd,
    unsigned char *data, unsigned int datalen);
static void tn2217_comport_start(struct term_s *t);

/* Access the private state structure of the terminal */
static struct tn2217_state *
tn2217_state(struct term_s *t)
{
    return (struct tn2217_state *)t->priv;
}
#define STATE(t) tn2217_state(t)

/* Called when a TELNET option changes */
static void
tn2217_check_options_changed(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);

    /* Detect the first time that the COM-PORT option becomes acceptable
     * at both remote and local. */
    if (!s->can_comport &&
         s->opt[TELOPT_COMPORT].us == YES &&
         s->opt[TELOPT_COMPORT].him == YES)
    {
        s->can_comport = 1;
        tn2217_comport_start(t);
    }
}

/* Starts protocol to enable/disable a peer option (sends DO/DONT). */
static int
tn2217_remote_opt(struct term_s *t, unsigned char opt, int want)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char msg[3] = { IAC, 0, opt };

    if (q->him == want ? NO : YES) {
        msg[1] = want ? DO : DONT;
        if (writen_ni(t->fd, msg, sizeof msg) == -1)
            return -1;
        q->him = want ? WANTYES : WANTNO;
    } else if (q->him == WANTNO) {
        q->himq = want ? OPPOSITE : EMPTY;
    } else if (q->him == WANTYES) {
        q->himq = want ? EMPTY : OPPOSITE;
    }
    tn2217_check_options_changed(t);
    return 0;
}

/* Starts protocol to enable/disable a local option (sends WILL/WONT). */
static int
tn2217_local_opt(struct term_s *t, unsigned char opt, int want)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char msg[3] = { IAC, 0, opt };

    if (q->us == want ? NO : YES) {
        msg[1] = want ? WILL : WONT;
        if (writen_ni(t->fd, msg, sizeof msg) == -1)
            return -1;
        q->us = want ? WANTYES : WANTNO;
    } else if (q->us == WANTNO) {
        q->usq = want ? OPPOSITE : EMPTY;
    } else if (q->us == WANTYES) {
        q->usq = want ? EMPTY : OPPOSITE;
    }
    tn2217_check_options_changed(t);
    return 0;
}

#define tn2217_do(t, opt)   tn2217_remote_opt(t, opt, 1)
#define tn2217_dont(t, opt) tn2217_remote_opt(t, opt, 0)
#define tn2217_will(t, opt) tn2217_local_opt(t, opt, 1)
#define tn2217_wont(t, opt) tn2217_local_opt(t, opt, 0)


/* Receive a WILL/WONT/DO/DONT message, and update our local state. */
static void
tn2217_recv_opt(struct term_s *t, unsigned char op, unsigned char opt)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char respond = 0;

    /* See RFC1143 for detailed explanation of the following logic.
     * It is a transliteration & compacting of the RFC's algorithm. */
    switch (op) {
    case WILL:
        if (q->him == NO) {
            if (tn2217_remote_should(t, opt)) {
                q->him = YES;
                respond = DO;
            } else {
                respond = DONT;
            }
        } else if (q->him == WANTNO) {
            q->him = (q->himq == EMPTY) ? NO : YES;
            q->himq = EMPTY;
        } else if (q->him == WANTYES) {
            if (q->himq == EMPTY)
                q->him = YES;
            else {
                q->him = WANTNO;
                q->himq = EMPTY;
                respond = DONT;
            }
        }
        break;
    case WONT:
        if (q->him == YES) {
            q->him = NO;
            respond = DONT;
        } else if (q->him == WANTNO) {
            if (q->himq == EMPTY)
                q->him = NO;
            else {
                q->him = WANTYES;
                q->himq = EMPTY;
                respond = DO;
            }
        } else if (q->him == WANTYES) {
            q->him = NO;
            q->himq = EMPTY;
        }
        break;
    case DO:
        if (q->us == NO) {
            if (tn2217_local_should(t, opt)) {
                q->us = YES;
                respond = WILL;
            } else {
                respond = WONT;
            }
        } else if (q->us == WANTNO) {
            q->us = (q->usq == EMPTY) ? NO : YES;
            q->usq = EMPTY;
        } else if (q->us == WANTYES) {
            if (q->usq == EMPTY)
                q->us = YES;
            else {
                q->us = WANTNO;
                q->usq = EMPTY;
                respond = WONT;
            }
        }
        break;
    case DONT:
        if (q->us == YES) {
            q->us = NO;
            respond = WONT;
        } else if (q->us == WANTNO) {
            if (q->usq == EMPTY)
                q->us = NO;
            else {
                q->us = WANTYES;
                q->usq = EMPTY;
                respond = WILL;
            }
        } else if (q->us == WANTYES) {
            q->us = NO;
            q->usq = EMPTY;
        }
        break;
    }

    if (respond) {
        unsigned char msg[3] = { IAC, respond, opt };
        writen_ni(t->fd, msg, sizeof msg);
    }
    tn2217_check_options_changed(t);
}

/* Receive and process a single byte of an IAC command.
 * The caller will have appended the byte to s->cmdbuf[] already.
 * Completion of a command is signaled by setting s->cmdbuflen to 0. */
static void
tn2217_recv_cmd_partial(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);
    unsigned char *cmd = s->cmdbuf;
    unsigned int cmdlen = s->cmdbuflen;

    /* assert(cmdlen > 1) */

    /* Note: Normal completion is indicated with 'break'.
     * For incomplete commands, return with 'goto incomplete'. */
    switch (cmd[1]) {
    case WILL: case WONT: case DO: case DONT:
        if (cmdlen < 3) /* {IAC [DO|...] opt} */
            goto incomplete;
        tn2217_recv_opt(t, cmd[1], cmd[2]);
        break;
    case SB:            /* {IAC SB ... IAC SE} */
        if (cmdlen < 3) {
            s->cmdiac = 0;
            goto incomplete;
        }
        if (!s->cmdiac && cmd[cmdlen -1] == IAC) {
            s->cmdiac = 1;
            s->cmdbuflen--;
            goto incomplete;
        }
        if (!s->cmdiac)
            goto incomplete;
        s->cmdiac = 0;
        if (cmd[cmdlen - 1] == IAC)
            goto incomplete; /* IAC left at end of cmd[] */
        if (cmd[--cmdlen] != SE) { /* remove trailing SE */
            fprintf(stderr, "[BAD TELNET SB]");
            break;
        }
        if (cmdlen >= 4 && cmd[2] == TELOPT_COMPORT)
            tn2217_recv_comport_cmd(t, cmd[3], cmd + 4, cmdlen - 4);
        break;
    case AYT:
        fprintf(stderr, "[REMOTE AYT]");
        /* writen_ni(t->fd, "\377\361", 2); */ /* respond with NOP? */
        break;
    case BREAK:
        fprintf(stderr, "[REMOTE BREAK]");
        break;
    case IP:
        fprintf(stderr, "[REMOTE INTERRUPT]");
        break;
    default:
        /* Ignore everything else */
        break;
    }
    s->cmdbuflen = 0;
incomplete:
    return;
}

/* Should the remote enable its option if it tells us it WILL/WONT? */
static int
tn2217_remote_should(struct term_s *t, unsigned char opt)
{
    return opt == TELOPT_BINARY ||
           opt == TELOPT_SGA ||
           opt == TELOPT_COMPORT;
}

/* Should we enable a local option if remote asks us to DO/DONT? */
static int
tn2217_local_should(struct term_s *t, unsigned char opt)
{
    return opt == TELOPT_BINARY ||
           opt == TELOPT_SGA ||
           opt == TELOPT_COMPORT;
}

/* Opens a TCP socket to a host. Returns -1 on failure.  */
int
tn2217_open(const char *port)
{
    struct addrinfo *ais = NULL;
    struct addrinfo *ai;
    struct addrinfo hints;
    char *node;
    const char *service = "telnet";
    int error;
    char *sep = NULL;
    int fd = -1;
    long fl;

    /* Allow a port number to be specified with a comma */
    node = strdup(port);
    sep = strrchr(node, ',');
    if ( sep ) {
        *sep++ = '\0';
        service = sep;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;

    error = getaddrinfo(node, service, &hints, &ais);
    if ( error ) {
        fd_printf(STDERR_FILENO, "%s/%s: %s\r\n", node, service,
                gai_strerror(error));
        goto out;
    }

    /* Attempt each address in sequence */
    for ( ai = ais; ai; ai = ai->ai_next ) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if ( fd == -1 )
            continue;
        if ( connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 )
            break;
        close(fd);
        fd = -1;
    }
    if ( fd == -1 )
        fd_printf(STDERR_FILENO, "%s/%s: %s\r\n", node, service,
                strerror(errno));

    freeaddrinfo(ais);

    if ( fd != -1 ) {
        /* Enable nonblocking mode */
                /* Not all systems can pass SOCK_NONBLOCK to socket() */
        fl = fcntl(fd, F_GETFL);
        fl |= O_NONBLOCK;
        fcntl(fd, F_SETFL, fl);
    }

out:
    free(node);
    return fd;
}

/* Sends {IAC SB COMPORT <cmd> <data> IAC SE} */
static void
tn2217_send_comport_cmd(struct term_s *t, unsigned char cmd,
    const void *data, unsigned int datalen)
{
    unsigned char msg[4] = { IAC, SB, TELOPT_COMPORT, cmd };
    static const unsigned char end[2] = { IAC, SE };

    /* assert(cmd != IAC); */
    writen_ni(t->fd, msg, sizeof msg);
    if (datalen)
        tn2217_write(t, data, datalen);
    writen_ni(t->fd, end, sizeof end);
}

/* Sends a COM-PORT command consisting of a single byte (a common case) */
static void
tn2217_send_comport_cmd1(struct term_s *t, unsigned char cmd,
    unsigned char data)
{
    tn2217_send_comport_cmd(t, cmd, &data, 1);
}

/* Sends a COM-PORT command with a 4-byte integer argument */
static void
tn2217_send_comport_cmd4(struct term_s *t, unsigned char cmd, unsigned int val)
{
    unsigned char valbuf[4];

    /* Convert the value to network endian */
    valbuf[0] = (val >> 24) & 0xff;
    valbuf[1] = (val >> 16) & 0xff;
    valbuf[2] = (val >>  8) & 0xff;
    valbuf[3] = (val >>  0) & 0xff;
    tn2217_send_comport_cmd(t, cmd, valbuf, sizeof valbuf);
}

static unsigned int
decode_speed(speed_t speed)
{
    /* TODO: use baud_table[] */
    switch (speed) {
    case B0: return 0;
    case B50: return 50;
    case B75: return 75;
    case B110: return 110;
    case B134: return 134;
    case B150: return 150;
    case B200: return 200;
    case B300: return 300;
    case B1200: return 1200;
    case B1800: return 1800;
    case B2400: return 2400;
    case B4800: return 4800;
    case B9600: return 9600;
    case B19200: return 19200;
    case B38400: return 38400;
    case B57600: return 57600;
    case B115200: return 115200;
    default: return (unsigned int)speed;
    }
}

/* Return simple debug representation of a termios, eg "19200,8n1" */
const char *
termios_repr(const struct termios *tio)
{
    static char out[256];
    int c = tio->c_cflag;

    snprintf(out, sizeof out,
        "%u,%u%c%u%s",
        decode_speed(cfgetospeed(tio)),
            (c & CSIZE) == CS5 ? 5 :
            (c & CSIZE) == CS6 ? 6 :
            (c & CSIZE) == CS7 ? 7 : 8,
            (c & (PARENB|PARODD)) == PARENB ? 'e' :
            (c & (PARENB|PARODD)) == PARODD ? 'o' : 'n',
            (c & CSTOPB) ? 2 : 1,
            (c & CRTSCTS) ? ",crtscts" : "");
    return out;
}

/* Return simple debug representation of a modem bits, eg "<dtr,cd>" */
const char *
modem_repr(int m)
{
    static char out[256];

    snprintf(out, sizeof out,
        "<%s%s%s%s%s%s%s%s%s>",
        (m & TIOCM_LE)  ? ",dsr": "",
        (m & TIOCM_DTR) ? ",dtr": "",
        (m & TIOCM_RTS) ? ",rts": "",
        (m & TIOCM_ST)  ? ",st" : "",
        (m & TIOCM_SR)  ? ",sr" : "",
        (m & TIOCM_CTS) ? ",cts": "",
        (m & TIOCM_CD)  ? ",cd" : "",
        (m & TIOCM_RI)  ? ",ri" : "",
        (m & TIOCM_DSR) ? ",dsr": ""
    );
    if (out[1] == ',') {
        out[1] = '<';
        return &out[1];
    } else
        return "<>";
}


/* Sends a SET-BAUDRATE message to the remote */
static void
tn2217_send_set_baudrate(struct term_s *t, speed_t speed)
{
    tn2217_send_comport_cmd4(t, COMPORT_SET_BAUDRATE, decode_speed(speed));
}

/* Sends a SET-DATASIZE message to the remote */
static void
tn2217_send_set_datasize(struct term_s *t, int c_flag)
{
    unsigned char val;

    switch (c_flag & CSIZE) {
    case CS5:  val = COMPORT_DATASIZE_5; break;
    case CS6:  val = COMPORT_DATASIZE_6; break;
    case CS7:  val = COMPORT_DATASIZE_7; break;
    default:   val = COMPORT_DATASIZE_8; break;
    }
    tn2217_send_comport_cmd1(t, COMPORT_SET_DATASIZE, val);
}

/* Sends a SET-PARITY message to the remote */
static void
tn2217_send_set_parity(struct term_s *t, int c_flag)
{
    unsigned char val;

    switch (c_flag & PARMASK) {
    case PARENB | PARODD: val = COMPORT_PARITY_ODD; break;
    case PARENB         : val = COMPORT_PARITY_EVEN; break;
    default:              val = COMPORT_PARITY_NONE; break;
    }
    tn2217_send_comport_cmd1(t, COMPORT_SET_PARITY, val);
}

/* Sends a SET-STOPSIZE message to the remote */
static void
tn2217_send_set_stopsize(struct term_s *t, int c_flag)
{
    if (c_flag & CSTOPB)
        tn2217_send_comport_cmd1(t, COMPORT_SET_PARITY, COMPORT_STOPSIZE_2);
    else
        tn2217_send_comport_cmd1(t, COMPORT_SET_PARITY, COMPORT_STOPSIZE_1);
}

/* Sends a SET-CONTROL message to control hardware flow control */
static void
tn2217_send_set_fc(struct term_s *t, int c_flag)
{
    unsigned char val;

    if (c_flag & CRTSCTS)
        val = COMPORT_CONTROL_FC_HARDWARE;
    else
        val = COMPORT_CONTROL_FC_NONE;
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL, val);
}

/* Sends a SET-CONTROL message to control remote DTR state */
static void
tn2217_send_set_dtr(struct term_s *t, int modem)
{
    unsigned char val;

    if (modem & TIOCM_DTR)
        val = COMPORT_CONTROL_DTR_ON;
    else
        val = COMPORT_CONTROL_DTR_OFF;
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL, val);
}

/* Sends a SET-CONTROL message to control remote DTR state */
static void
tn2217_send_set_rts(struct term_s *t, int modem)
{
    unsigned char val;

    if (modem & TIOCM_RTS)
        val = COMPORT_CONTROL_RTS_ON;
    else
        val = COMPORT_CONTROL_RTS_OFF;
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL, val);
}


/* Called when the COM-PORT option frist becomes available.
 * Sends initial COM-PORT setup, and requests the remote send its
 * COM-PORT state. */
static void
tn2217_comport_start(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);

    /* Request the remote's server identifier. (Optional) */
    tn2217_send_comport_cmd(t, COMPORT_SIGNATURE, "", 0);

    /* Ask for ongoing status updates for modem signal lines */
    tn2217_send_comport_cmd1(t, COMPORT_SET_LINESTATE_MASK, 0);
    tn2217_send_comport_cmd1(t, COMPORT_SET_MODEMSTATE_MASK, MODEMSTATE_MASK);

    if (s->set_termios) {
        tn2217_send_set_baudrate(t, cfgetospeed(&s->termios));
        tn2217_send_set_datasize(t, s->termios.c_cflag);
        tn2217_send_set_parity(t, s->termios.c_cflag);
        tn2217_send_set_stopsize(t, s->termios.c_cflag);
        tn2217_send_set_fc(t, s->termios.c_cflag);
    } else {
        /* If we're not going to specify it, ask for
         * the current com port geometry. */
        tn2217_send_comport_cmd4(t, COMPORT_SET_BAUDRATE,
                                    COMPORT_BAUDRATE_REQUEST);
        tn2217_send_comport_cmd1(t, COMPORT_SET_DATASIZE,
                                    COMPORT_DATASIZE_REQUEST);
        tn2217_send_comport_cmd1(t, COMPORT_SET_PARITY,
                                    COMPORT_PARITY_REQUEST);
        tn2217_send_comport_cmd1(t, COMPORT_SET_STOPSIZE,
                                    COMPORT_STOPSIZE_REQUEST);
        tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL,
                                    COMPORT_CONTROL_FC_REQUEST);
    }

    if (s->set_modem) {
        tn2217_send_set_dtr(t, s->modem);
        tn2217_send_set_rts(t, s->modem);
    } else {
        tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL,
                                    COMPORT_CONTROL_DTR_REQUEST);
        tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL,
                                    COMPORT_CONTROL_RTS_REQUEST);
    }

    /* Also ask for the current break state */
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL,
                                    COMPORT_CONTROL_BREAK_REQUEST);
}

/* Handle receipt of {IAC SB COMPORT <cmd> <data> IAC SE} command. */
static void
tn2217_recv_comport_cmd(struct term_s *t, unsigned char cmd,
    unsigned char *data, unsigned int datalen)
{
    struct tn2217_state *s = STATE(t);
    int val;
    struct termios *tio = &s->termios;
    int *modem = &s->modem;

    /* The server sends its responses using IDs offset from
     * COMPORT_SERVER_BASE. */
    if (cmd < COMPORT_SERVER_BASE)
        return;
    cmd -= COMPORT_SERVER_BASE;

    switch (cmd) {

    case COMPORT_SIGNATURE: /* Exchange software "signature" text */
        /* This is optional, but we might later exploit it to achieve
         * server-specific workarounds. */
        if (datalen)
            fprintf(stderr, "[REMOTE SIGNATURE: '%.*s']\r\n", datalen, data);
        else
            tn2217_send_comport_cmd(t, COMPORT_SIGNATURE, "picocom", 7);
        break;

    case COMPORT_SET_BAUDRATE: /* Remote baud rate update */
        if (datalen >= 4) {
            unsigned int baud = (data[0] << 24) | (data[1] << 16) |
                             (data[2] << 8) | data[3];
            speed_t speed;
            /* The remote's speed may not be an exact termios baud */
            if      (baud <=     50) speed = B50;
            else if (baud <=     75) speed = B75;
            else if (baud <=    110) speed = B110;
            else if (baud <=    134) speed = B134;
            else if (baud <=    150) speed = B150;
            else if (baud <=    200) speed = B200;
            else if (baud <=    300) speed = B300;
            else if (baud <=    600) speed = B600;
            else if (baud <=   1200) speed = B1200;
            else if (baud <=   1800) speed = B1800;
            else if (baud <=   2400) speed = B2400;
            else if (baud <=   4800) speed = B4800;
            else if (baud <=   9600) speed = B9600;
            else if (baud <=  19200) speed = B19200;
            else if (baud <=  38400) speed = B38400;
            else if (baud <=  57600) speed = B57600;
            else if (baud <= 115200) speed = B115200;
            else                     speed = baud;
            cfsetospeed(&s->termios, speed);
            cfsetispeed(&s->termios, B0);
        }
        /* XXX the sredird server sends an extra 4-byte value,
         * which looks like the ispeed. It is not in the RFC. */
        break;

    case COMPORT_SET_DATASIZE: /* Notification of remote data bit size */
        val = -1;
        if (datalen >= 1) {
            switch (data[0]) {
            case COMPORT_DATASIZE_5: val = CS5; break;
            case COMPORT_DATASIZE_6: val = CS6; break;
            case COMPORT_DATASIZE_7: val = CS7; break;
            case COMPORT_DATASIZE_8: val = CS8; break;
            }
        }
        if (val != -1) {
            tio->c_cflag &= ~CSIZE;
            tio->c_cflag |= val;
        }
        break;

    case COMPORT_SET_PARITY: /* Remote parity update */
        val = -1;
        if (datalen >= 1) {
            switch (data[0]) {
            case COMPORT_PARITY_NONE: val = 0; break;
            case COMPORT_PARITY_ODD:  val = PARENB | PARODD; break;
            case COMPORT_PARITY_EVEN: val = PARENB; break;
            }
        }
        if (val != -1) {
            tio->c_cflag &= ~PARMASK;
            tio->c_cflag |= val;
        }
        break;

    case COMPORT_SET_STOPSIZE: /* Remote stop bits update */
        val = -1;
        if (datalen >= 1) {
            switch (data[0]) {
            case COMPORT_STOPSIZE_1: val = 0; break;
            case COMPORT_STOPSIZE_2: val = CSTOPB; break;
            }
        }
        if (val != -1) {
            tio->c_cflag &= ~CSTOPB;
            tio->c_cflag |= val;
        }
        break;

    case COMPORT_SET_CONTROL: /* Remote control state update */
        if (datalen >= 1) {
            switch (data[0]) {
            /* Flow control changes and COMPORT_CONTROL_FC_REQUEST reply */
            case COMPORT_CONTROL_FC_NONE:
            case COMPORT_CONTROL_FC_XONOFF:
            case COMPORT_CONTROL_FC_DCD:
            case COMPORT_CONTROL_FC_DSR:
            case COMPORT_CONTROL_FC_HARDWARE:
                val = (data[0] == COMPORT_CONTROL_FC_HARDWARE ) ? CRTSCTS : 0;
                tio->c_cflag &= ~CRTSCTS;
                tio->c_cflag |= val;
                break;
            /* DTR changes and COMPORT_CONTROL_DTR_REQUEST reply */
            case COMPORT_CONTROL_DTR_ON:
            case COMPORT_CONTROL_DTR_OFF:
                val = (data[0] == COMPORT_CONTROL_DTR_ON) ? TIOCM_DTR : 0;
                *modem &= ~TIOCM_DTR;
                *modem |= val;
                DEBUG("[notified: dtr=%u]", !!val);
                break;
            /* RTS changes and COMPORT_CONTROL_RTS_REQUEST reply */
            case COMPORT_CONTROL_RTS_ON:
            case COMPORT_CONTROL_RTS_OFF:
                val = (data[0] == COMPORT_CONTROL_RTS_ON) ? TIOCM_RTS : 0;
                *modem &= ~TIOCM_RTS;
                *modem |= val;
                DEBUG("[notified: rts=%u]", !!val);
                break;
            }
        }
        break;

    case COMPORT_NOTIFY_MODEMSTATE: /* Remote modem signal update */
        /* Updates are masked by COMPORT_SET_MODEMSTATE_MASK elsewhere */
        if (datalen >= 1) {
            val = 0;
            if (data[0] & COMPORT_MODEM_CD)  val |= TIOCM_CD;
            if (data[0] & COMPORT_MODEM_RI)  val |= TIOCM_RI;
            if (data[0] & COMPORT_MODEM_DSR) val |= TIOCM_LE;
            if (data[0] & COMPORT_MODEM_CTS) val |= TIOCM_CTS;
            DEBUG("[notified: %s]", modem_repr(val));
            *modem &= ~(TIOCM_CD|TIOCM_RI|TIOCM_LE|TIOCM_CTS);
            *modem |= val;
        }
        break;
    }
}


static int
tn2217_init(struct term_s *t)
{
    struct tn2217_state *s;

    t->priv = calloc(1, sizeof (struct tn2217_state));
    if ( ! t->priv ) {
        return -1;
    }

    s = STATE(t);

    /* We don't yet know the remote's com port state, but
     * let's assume it is raw. We will ask for updates later when
     * the COM-PORT option is negotiated. */
    cfmakeraw(&s->termios);
    cfsetospeed(&s->termios, B0); /* This means 'unknown'. */
    cfsetispeed(&s->termios, B0); /* This means "same as ospeed" */

    /* Normally DTR and RTS are asserted, but an update can change that */
    s->modem = TIOCM_DTR | TIOCM_RTS;

    /* Start the negotiations. */
    tn2217_will(t, TELOPT_BINARY);
    tn2217_do(t, TELOPT_BINARY);
    tn2217_will(t, TELOPT_SGA);
    tn2217_do(t, TELOPT_SGA);
    tn2217_will(t, TELOPT_COMPORT);
    tn2217_do(t, TELOPT_COMPORT);

    return 0;
}

static void
tn2217_fini(struct term_s *t)
{
    free(t->priv);
}

static int
tn2217_tcgetattr(struct term_s *t, struct termios *termios_out)
{
    struct tn2217_state *s = STATE(t);

    DEBUG("[tcgetattr %s]", termios_repr(&s->termios));
    *termios_out = s->termios;
    return 0;
}

static int
tn2217_tcsetattr(struct term_s *t, int when, const struct termios *tio)
{
    struct tn2217_state *s = STATE(t);

    DEBUG("[tcsetattr %s]", termios_repr(tio));
    s->termios = *tio;
    if (s->can_comport) {
        tn2217_send_set_baudrate(t, cfgetospeed(tio));
        tn2217_send_set_datasize(t, tio->c_cflag);
        tn2217_send_set_parity(t, tio->c_cflag);
        tn2217_send_set_stopsize(t, tio->c_cflag);
        tn2217_send_set_fc(t, tio->c_cflag);
    } else
        s->set_termios = 1;
    return 0;
}

static int
tn2217_modem_get(struct term_s *t, int *modem_out)
{
    struct tn2217_state *s = STATE(t);

    DEBUG("[modem_get %s]", modem_repr(s->modem));
    *modem_out = s->modem;
    return 0;
}

static int
tn2217_modem_bis(struct term_s *t, const int *modem)
{
    struct tn2217_state *s = STATE(t);

    DEBUG("[modem_bis %s]", modem_repr(*modem));

    s->modem |= *modem;

    if (s->can_comport) {
        if (*modem & TIOCM_DTR)
            tn2217_send_set_dtr(t, TIOCM_DTR);
        if (*modem & TIOCM_RTS)
            tn2217_send_set_rts(t, TIOCM_RTS);
    } else if (*modem & (TIOCM_DTR|TIOCM_RTS))
        s->set_modem = 1;

    return 0;
}

static int
tn2217_modem_bic(struct term_s *t, const int *modem)
{
    struct tn2217_state *s = STATE(t);

    DEBUG("[modem_bic %s]", modem_repr(*modem));

    s->modem &= ~*modem;

    if (s->can_comport) {
        if (*modem & TIOCM_DTR)
            tn2217_send_set_dtr(t, 0);
        if (*modem & TIOCM_RTS)
            tn2217_send_set_rts(t, 0);
    } else if (*modem & (TIOCM_DTR|TIOCM_RTS))
        s->set_modem = 1;

    return 0;
}

static int
tn2217_send_break(struct term_s *t)
{
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL, COMPORT_CONTROL_BREAK_ON);
    usleep(250000); /* 250 msec */
    tn2217_send_comport_cmd1(t, COMPORT_SET_CONTROL, COMPORT_CONTROL_BREAK_OFF);
    return 0;
}

static int
tn2217_flush(struct term_s *t, int selector)
{
    unsigned char val;

    /* Purge, presumably so that we have something to flush */
    switch (selector) {
    case TCIFLUSH:  val = COMPORT_PURGE_RX; break;
    case TCOFLUSH:  val = COMPORT_PURGE_TX; break;
    default:        val = COMPORT_PURGE_RXTX; break;
    }
    tn2217_send_comport_cmd1(t, COMPORT_PURGE_DATA, val);
    return 0;
}

static int
tn2217_drain(struct term_s *t)
{
    return 0;
}

/* Reads raw binary from the socket and immediately handles any
 * in-stream TELNET commands. */
static int
tn2217_read(struct term_s *t, void *buf, unsigned bufsz)
{
    struct tn2217_state *s = STATE(t);
    unsigned char *in, *out;
    int inlen, outlen;
    unsigned char *iac;

    inlen = read(t->fd, buf, bufsz);
    if (inlen <= 0)
        return inlen;

    in = out = (unsigned char *)buf;
    outlen = 0;

    while (inlen) {
        while (s->cmdbuflen && inlen) {
            /* We are currently appending to the command accumulator. */
            if (s->cmdbuflen >= sizeof s->cmdbuf - 1) {
                s->cmdbuflen = 0; /* Abandon on overflow */
                fprintf(stderr, "[overlong IAC command]\r\n");
                break;
            }
            s->cmdbuf[s->cmdbuflen++] = *in++;
            inlen--;
            if (s->cmdbuflen == 2 && s->cmdbuf[1] == IAC) {
                /* Handle {IAC IAC} escaping immediately */
                *out++ = IAC;
                outlen++;
                s->cmdbuflen = 0;
            } else {
                /* Handle {IAC ...} accumulations byte-at-a-time.
                 * s->cmdbuflen will be cleared on each completion */
                tn2217_recv_cmd_partial(t);
            }
        }

        /* Not currently in an IAC command. Look for the start of one. */
        iac = (unsigned char *)memchr(in, IAC, inlen);
        if (iac) {
            int priorlen = iac - in;

            /* Copy out the user data prior to the IAC */
            memmove(out, in, priorlen);
            out += priorlen;
            outlen += priorlen;

            /* Make cmdbuf[] non-empty to indicate that we've entered
             * a TELNET command */
            s->cmdbuf[0] = IAC;
            s->cmdbuflen = 1;

            /* Remove the IAC and copied-out data from the input buffer */
            inlen -= priorlen + 1;
            in += priorlen + 1;
        } else {
            /* Everything is user data. Copy it out */
            memmove(out, in, inlen);
            outlen += inlen;
            inlen = 0;
        }
    }

    if (outlen == 0) {
        /* If all we processed were TELNET commands, we can't return 0
         * because that would falsely indicate an EOF condition. Instead,
         * signal EAGAIN. See the article "Worse is Better". */
        errno = EAGAIN;
        outlen = -1;
    }

    return outlen;
}

static int
tn2217_write(struct term_s *t, const void *buf, unsigned bufsz)
{
    const unsigned char *start, *end;
    unsigned int len;

    len = bufsz;
    start = (const unsigned char *)buf;

    /* Double instances of IAC by arranging for overlapping writes. */
    end = (const unsigned char *)memchr(start, IAC, len);
    while (end) {
        if (write(t->fd, start, (end - start) + 1) < 0)
            return -1;
        len -= end - start;
        start = end;
        end = (const unsigned char *)memchr(start + 1, IAC, len - 1);
    }

    if (write(t->fd, start, len) < 0)
        return -1;
    return bufsz;
}

const struct term_ops tn2217_ops = {
    .init = tn2217_init,
    .fini = tn2217_fini,
    .tcgetattr = tn2217_tcgetattr,
    .tcsetattr = tn2217_tcsetattr,
    .modem_get = tn2217_modem_get,
    .modem_bis = tn2217_modem_bis,
    .modem_bic = tn2217_modem_bic,
    .send_break = tn2217_send_break,
    .flush = tn2217_flush,
    .drain = tn2217_drain,
    .read = tn2217_read,
    .write = tn2217_write,
};

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
