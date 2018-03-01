/* vi: set sw=4 ts=4:
 *
 * tn2217.c
 *
 * TELNET and COM-PORT (RFC2217) remote terminal protocol.
 *
 * Provides a network virtual terminal over a TELNET TCP connection.
 * Uses the TELNET COM PORT (RFC2217) option which provides control
 * over baud rate, parity, data bits and modem control lines at a com
 * port server.
 *
 * See also:
 *   https://tools.ietf.org/html/rfc854 (TELNET protocol)
 *   https://tools.ietf.org/html/rfc856 (BINARY option)
 *   https://tools.ietf.org/html/rfc857 (ECHO option)
 *   https://tools.ietf.org/html/rfc858 (SGA option)
 *   https://tools.ietf.org/html/rfc2217 (COMPORT option)
 *   https://tools.ietf.org/html/rfc1143 (Q method for option negotiation)
 *
 *   https://sourceforge.net/projects/sredird/ (sredird RFC2217 server)
 *   http://www.columbia.edu/kermit/ftp/ (sredird from old Kermit project)
 *   https://sourceforge.net/projects/ser2net/ (another RFC2217 server)
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
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>

#include "fdio.h"
#include "termint.h"
#include "tncomport.h"
#include "tn2217.h"

#include <sys/ioctl.h>
#include <arpa/telnet.h>

#if 1
/* Disable debugging code altogether */
#define DEBUG 0
#define DB(fl, ...) /* nothing */
#define DBG(...) /* nothing */
#define DB_ON(fl) 0
#else
/* Enable debugging code */
#define DEBUG 1
#define DB(fl, ...)                                 \
    do {                                            \
        if ( DB_MASK & (fl) )                       \
            fd_printf(STDERR_FILENO, __VA_ARGS__);  \
    } while (0)
#define DBG(...) DB(DB_OTH, __VA_ARGS__)
#define DB_ON(fl) (DB_MASK & (fl))
/* Debugging message groups */
#define DB_OTH (1 << 0) /* other */
#define DB_NEG (1 << 1) /* option negotiation messages */
#define DB_OPT (1 << 2) /* option values */
#define DB_CMP (1 << 3) /* comport messages */
/* Enable specific groups */
#define DB_MASK (DB_OTH | DB_NEG /* | DB_OPT */ | DB_CMP)
#endif

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

    int conf_pending;           /* # of not-replied config commands */
    /* When COM-PORT option is fully negotiated, can_comport
     * is set to true. This also means that any deferred termios/modem
     * settings will be triggered. */
    unsigned int can_comport : 1,  /* WILL COMPORT was received */
                 set_termios : 1,  /* tcsetattr called before can_comport */
                 set_modem : 1,    /* modem_bis/c called before can_comport */
                 configed: 1;      /* INITIAL config complete */

};


static int remote_should(struct term_s *t, unsigned char opt);
static int local_should(struct term_s *t, unsigned char opt);
static int escape_write(struct term_s *t, const void *buf, unsigned bufsz);
static int read_and_proc(struct term_s *t, void *buf, unsigned bufsz);
static int comport_recv_cmd(struct term_s *t, unsigned char cmd,
                            unsigned char *data, unsigned int datalen);
static int comport_start(struct term_s *t);

/* Access the private state structure of the terminal */
static struct tn2217_state *
tn2217_state(struct term_s *t)
{
    return (struct tn2217_state *)t->priv;
}
#define STATE(t) tn2217_state(t)

#if DEBUG

/* Some debug helpers (do not use in non-debugging code, they will go
   away) */

static const char *
str_nn(unsigned char i)
{
    static const char *names[256];
    if ( ! names[i] ) {
        char *v;
        v = malloc(4);
        snprintf(v, 4, "%d", i);
        names[i] = v;
    }
    return names[i];
}

static const char *
str_cmd(unsigned char cmd) {
    static const char *names[] = {
        "WILL", "WONT", "DO", "DONT"
    };
    return  ( (cmd < WILL || cmd > DONT)
              ? str_nn(cmd) : names[cmd - WILL] );
}

static const char *
str_opt(unsigned char opt) {
    static const char *names[] = {
        [TELOPT_BINARY] = "BINARY",
        [TELOPT_ECHO] = "ECHO",
        [TELOPT_SGA] = "SGA",
        [TELOPT_COMPORT] = "COMPORT",
        [255] = NULL
    };
    return names[opt] ? names[opt] : str_nn(opt);
}

static const char *
str_optval(int val) {
    static const char *names[] = {
        "NO", "YES", "WANTYES", "WANTNO"
    };
    return names[(unsigned)val&3];
}

/* Return simple debug representation of a termios,
   eg "19200:8:none:1:RTS/CTS" */
const char *
repr_termios(const struct termios *tio)
{
    static char out[256];

    snprintf(out, sizeof out,
             "%u:%u:%s:%u:%s",
             tios_get_baudrate(tio, NULL),
             tios_get_databits(tio),
             parity_str[tios_get_parity(tio)],
             tios_get_stopbits(tio),
             flow_str[tios_get_flowcntrl(tio)]);
    return out;
}

/* Return simple debug representation of a modem bits, eg "<dtr,cd>" */
const char *
repr_modem(int m)
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

#endif /* of DEBUG */

/* Called when a TELNET option changes */
static int
check_options_changed(struct term_s *t, unsigned char opt)
{
    struct tn2217_state *s = STATE(t);
    int rval = 0;

    DB(DB_OPT, "[opt %s %s %s]\r\n", str_opt(opt),
       str_optval(s->opt[opt].us), str_optval(s->opt[opt].him));

    /* Detect the first time that the COM-PORT option becomes acceptable. */
    if ( !s->can_comport &&
         s->opt[TELOPT_COMPORT].us == YES )
    {
        s->can_comport = 1;
        rval = comport_start(t);
    }
    return rval;
}

/* Starts protocol to enable/disable a peer option (sends DO/DONT). */
static int
remote_opt(struct term_s *t, unsigned char opt, int want)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char msg[3] = { IAC, 0, opt };

    if (q->him == want ? NO : YES) {
        msg[1] = want ? DO : DONT;
        if ( writen_ni(t->fd, msg, sizeof msg) != sizeof msg ) {
            term_errno = TERM_EOUTPUT; return -1;
        }
        DB(DB_NEG, "[sent: %s %s]\r\n", str_cmd(msg[1]), str_opt(opt));
        q->him = want ? WANTYES : WANTNO;
    } else if (q->him == WANTNO) {
        q->himq = want ? OPPOSITE : EMPTY;
    } else if (q->him == WANTYES) {
        q->himq = want ? EMPTY : OPPOSITE;
    }
    return check_options_changed(t, opt);
}

/* Starts protocol to enable/disable a local option (sends WILL/WONT). */
static int
local_opt(struct term_s *t, unsigned char opt, int want)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char msg[3] = { IAC, 0, opt };

    if (q->us == want ? NO : YES) {
        msg[1] = want ? WILL : WONT;
        if ( writen_ni(t->fd, msg, sizeof msg) != sizeof msg ) {
            term_errno = TERM_EOUTPUT; return -1;
        }
        DB(DB_NEG, "[sent: %s %s]\r\n", str_cmd(msg[1]), str_opt(opt));
        q->us = want ? WANTYES : WANTNO;
    } else if (q->us == WANTNO) {
        q->usq = want ? OPPOSITE : EMPTY;
    } else if (q->us == WANTYES) {
        q->usq = want ? EMPTY : OPPOSITE;
    }
    return check_options_changed(t, opt);
}

#define opt_do(t, opt)   remote_opt(t, opt, 1)
#define opt_dont(t, opt) remote_opt(t, opt, 0)
#define opt_will(t, opt) local_opt(t, opt, 1)
#define opt_wont(t, opt) local_opt(t, opt, 0)

/* Receive a WILL/WONT/DO/DONT message, and update our local state. */
static int
recv_opt(struct term_s *t, unsigned char op, unsigned char opt)
{
    struct tn2217_state *s = STATE(t);
    struct q_option *q = &s->opt[opt];
    unsigned char respond = 0;

    DB(DB_NEG, "[received: %s %s]\r\n", str_cmd(op), str_opt(opt));

    /* See RFC1143 for detailed explanation of the following logic.
     * It is a transliteration & compacting of the RFC's algorithm. */
    switch (op) {
    case WILL:
        if (q->him == NO) {
            if (remote_should(t, opt)) {
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
            if (local_should(t, opt)) {
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
        if ( writen_ni(t->fd, msg, sizeof msg) != sizeof msg) {
            term_errno = TERM_EOUTPUT; return -1;
        }
        DB(DB_NEG, "[sent: %s %s]\r\n", str_cmd(respond), str_opt(opt));
    }
    return check_options_changed(t, opt);
}

/* Receive and process a single byte of an IAC command.
 * The caller will have appended the byte to s->cmdbuf[] already.
 * Completion of a command is signaled by setting s->cmdbuflen to 0. */
static int
recv_cmd_partial(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);
    unsigned char *cmd = s->cmdbuf;
    unsigned int cmdlen = s->cmdbuflen;
    int rval = 0;

    /* assert(cmdlen > 1) */

    /* Note: Normal completion is indicated with 'break'.
     * For incomplete commands, return with 'goto incomplete'. */
    switch (cmd[1]) {
    case WILL: case WONT: case DO: case DONT:
        if (cmdlen < 3) /* {IAC [DO|...] opt} */
            goto incomplete;
        rval = recv_opt(t, cmd[1], cmd[2]);
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
            rval = comport_recv_cmd(t, cmd[3], cmd + 4, cmdlen - 4);
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
    return rval < 0 ? rval : 1;
incomplete:
    return rval < 0 ? rval : 0;
}

/* Should the remote enable its option if it tells us it WILL/WONT? */
static int
remote_should(struct term_s *t, unsigned char opt)
{
    return opt == TELOPT_BINARY ||
        opt == TELOPT_ECHO ||
        opt == TELOPT_SGA;
}

/* Should we enable a local option if remote asks us to DO/DONT? */
static int
local_should(struct term_s *t, unsigned char opt)
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

/* Closes a TCP socket to a host. See "tn2217.h".  */
int
tn2217_close(int fd, int drain)
{
    char buff[1024];
    long fl;
    int n;

    if ( ! drain )
        return close(fd);

    /* FIXME(npat): Maybe protect the "reading until server closes"
       with a large-ish timeout? */

    fl = fcntl(fd, F_GETFL);
    fl &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, fl);

    shutdown(fd, SHUT_WR);
    do {
        n = read(fd, buff, sizeof(buff));
    } while ( n > 0 );
    if ( n < 0 )
        return n;
    return close(fd);
}

/* Sends {IAC SB COMPORT <cmd> <data> IAC SE} */
static int
comport_send_cmd(struct term_s *t, unsigned char cmd,
                 const void *data, unsigned int datalen)
{
    unsigned char msg[4] = { IAC, SB, TELOPT_COMPORT, cmd };
    static const unsigned char end[2] = { IAC, SE };

    /* assert(cmd != IAC); */
    if ( writen_ni(t->fd, msg, sizeof msg) != sizeof msg )
        goto werror;
    if (datalen)
        if ( escape_write(t, data, datalen) != datalen )
            goto werror;
    if ( writen_ni(t->fd, end, sizeof end) != sizeof end )
        goto werror;

    return 0;

werror:
    term_errno = TERM_EOUTPUT;
    return -1;
}

/* Sends a COM-PORT command consisting of a single byte (a common case) */
static int
comport_send_cmd1(struct term_s *t, unsigned char cmd,
                  unsigned char data)
{
    return comport_send_cmd(t, cmd, &data, 1);
}

/* Sends a COM-PORT command with a 4-byte integer argument */
static int
comport_send_cmd4(struct term_s *t, unsigned char cmd, unsigned int val)
{
    unsigned char valbuf[4];

    /* Convert the value to network endian */
    valbuf[0] = (val >> 24) & 0xff;
    valbuf[1] = (val >> 16) & 0xff;
    valbuf[2] = (val >>  8) & 0xff;
    valbuf[3] = (val >>  0) & 0xff;
    return comport_send_cmd(t, cmd, valbuf, sizeof valbuf);
}

/* Sends a SET-BAUDRATE message to the remote */
static int
comport_send_set_baudrate(struct term_s *t, int speed)
{
    struct tn2217_state *s = STATE(t);
    int r;

    if (speed >= 0) {
        r = comport_send_cmd4(t, COMPORT_SET_BAUDRATE, speed);
        if ( r < 0 ) return r;
        s->conf_pending++;
    }
    return 0;
}

/* Sends a SET-DATASIZE message to the remote */
static int
comport_send_set_datasize(struct term_s *t, int databits)
{
    struct tn2217_state *s = STATE(t);
    unsigned char val;
    int r;

    switch (databits) {
    case 5:  val = COMPORT_DATASIZE_5; break;
    case 6:  val = COMPORT_DATASIZE_6; break;
    case 7:  val = COMPORT_DATASIZE_7; break;
    default: val = COMPORT_DATASIZE_8; break;
    }
    r = comport_send_cmd1(t, COMPORT_SET_DATASIZE, val);
    if ( r < 0 ) return r;
    s->conf_pending++;
    return 0;
}

/* Sends a SET-PARITY message to the remote */
static int
comport_send_set_parity(struct term_s *t, enum parity_e parity)
{
    struct tn2217_state *s = STATE(t);
    unsigned char val;
    int r;

    switch (parity) {
    case P_ODD:  val = COMPORT_PARITY_ODD; break;
    case P_EVEN: val = COMPORT_PARITY_EVEN; break;
    default:     val = COMPORT_PARITY_NONE; break;
    }
    r = comport_send_cmd1(t, COMPORT_SET_PARITY, val);
    if ( r < 0 ) return r;
    s->conf_pending++;
    return 0;
}

/* Sends a SET-STOPSIZE message to the remote */
static int
comport_send_set_stopsize(struct term_s *t, int stopbits)
{
    struct tn2217_state *s = STATE(t);
    int r;

    if (stopbits == 2)
        r = comport_send_cmd1(t, COMPORT_SET_STOPSIZE,
                                     COMPORT_STOPSIZE_2);
    else
        r = comport_send_cmd1(t, COMPORT_SET_STOPSIZE,
                                     COMPORT_STOPSIZE_1);
    if ( r < 0 ) return r;
    s->conf_pending++;
    return 0;
}

/* Sends a SET-CONTROL message to control hardware flow control */
static int
comport_send_set_fc(struct term_s *t, enum flowcntrl_e flow)
{
    struct tn2217_state *s = STATE(t);
    unsigned char val;
    int r;

    switch (flow) {
    case FC_RTSCTS:
        val = COMPORT_CONTROL_FC_HARDWARE;
        break;
    case FC_XONXOFF:
        val = COMPORT_CONTROL_FC_XONOFF;
        break;
    case FC_NONE:
    default:
        val = COMPORT_CONTROL_FC_NONE;
        break;
    }
    r = comport_send_cmd1(t, COMPORT_SET_CONTROL, val);
    if ( r < 0 ) return r;
    s->conf_pending++;
    return 0;
}

/* Sends a SET-CONTROL message to control remote DTR state */
static int
comport_send_set_dtr(struct term_s *t, int modem)
{
    unsigned char val;

    if (modem & TIOCM_DTR)
        val = COMPORT_CONTROL_DTR_ON;
    else
        val = COMPORT_CONTROL_DTR_OFF;
    return comport_send_cmd1(t, COMPORT_SET_CONTROL, val);
}

/* Sends a SET-CONTROL message to control remote RTS state */
static int
comport_send_set_rts(struct term_s *t, int modem)
{
    unsigned char val;

    if (modem & TIOCM_RTS)
        val = COMPORT_CONTROL_RTS_ON;
    else
        val = COMPORT_CONTROL_RTS_OFF;
    return comport_send_cmd1(t, COMPORT_SET_CONTROL, val);
}

static int
comport_config_port(struct term_s *t, const struct termios *tios)
{
    struct tn2217_state *s = STATE(t);
    int r;

    if (tios) {
        r = comport_send_set_baudrate(t, tios_get_baudrate(tios, NULL));
        if (r < 0) return r;
        r = comport_send_set_datasize(t, tios_get_databits(tios));
        if (r < 0) return r;
        r = comport_send_set_parity(t, tios_get_parity(tios));
        if (r < 0) return r;
        r = comport_send_set_stopsize(t, tios_get_stopbits(tios));
        if (r < 0) return r;
        r = comport_send_set_fc(t, tios_get_flowcntrl(tios));
        if (r < 0) return r;
    } else {
        /* If we're not going to specify it, ask for
         * the current com port geometry. */
        r = comport_send_cmd4(t, COMPORT_SET_BAUDRATE,
                              COMPORT_BAUDRATE_REQUEST);
        if (r < 0) return r;
        s->conf_pending ++;
        r = comport_send_cmd1(t, COMPORT_SET_DATASIZE,
                              COMPORT_DATASIZE_REQUEST);
        if (r < 0) return r;
        s->conf_pending ++;
        r = comport_send_cmd1(t, COMPORT_SET_PARITY,
                              COMPORT_PARITY_REQUEST);
        if (r < 0) return r;
        s->conf_pending ++;
        r = comport_send_cmd1(t, COMPORT_SET_STOPSIZE,
                              COMPORT_STOPSIZE_REQUEST);
        if (r < 0) return r;
        s->conf_pending ++;
        r = comport_send_cmd1(t, COMPORT_SET_CONTROL,
                              COMPORT_CONTROL_FC_REQUEST);
        if (r < 0) return r;
        s->conf_pending ++;
    }
    return 0;
}

/* Called when the COM-PORT option frist becomes available.
 * Sends initial COM-PORT setup, and requests the remote send its
 * COM-PORT state. */
static int
comport_start(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);
    int r;

    /* Request the remote's server identifier. (Optional) */
    r = comport_send_cmd(t, COMPORT_SIGNATURE, "", 0);
    if ( r < 0 ) return r;

    /* Ask for ongoing status updates for modem signal lines */
    r = comport_send_cmd1(t, COMPORT_SET_LINESTATE_MASK, 0);
    if ( r < 0 ) return r;
    r = comport_send_cmd1(t, COMPORT_SET_MODEMSTATE_MASK,
                          MODEMSTATE_MASK);
    if ( r < 0 ) return r;

    if (s->set_termios) {
        r = comport_config_port(t, &s->termios);
    } else {
        r = comport_config_port(t, NULL);
    }
    if ( r < 0 ) return r;

    if (s->set_modem) {
        r = comport_send_set_dtr(t, s->modem);
        if ( r < 0 ) return r;
        r = comport_send_set_rts(t, s->modem);
        if ( r < 0 ) return r;
    } else {
        r = comport_send_cmd1(t, COMPORT_SET_CONTROL,
                              COMPORT_CONTROL_DTR_REQUEST);
        if ( r < 0 ) return r;
        r = comport_send_cmd1(t, COMPORT_SET_CONTROL,
                              COMPORT_CONTROL_RTS_REQUEST);
        if ( r < 0 ) return r;
    }

    /* Also ask for the current break state */
    return comport_send_cmd1(t, COMPORT_SET_CONTROL,
                             COMPORT_CONTROL_BREAK_REQUEST);
}

/* Handle receipt of {IAC SB COMPORT <cmd> <data> IAC SE} command. */
static int
comport_recv_cmd(struct term_s *t, unsigned char cmd,
    unsigned char *data, unsigned int datalen)
{
    struct tn2217_state *s = STATE(t);
    int val;
    struct termios *tio = &s->termios;
    int *modem = &s->modem;
    int r;

    /* The server sends its responses using IDs offset from
     * COMPORT_SERVER_BASE. */
    if (cmd < COMPORT_SERVER_BASE)
        return 0;
    cmd -= COMPORT_SERVER_BASE;

    switch (cmd) {

    case COMPORT_SIGNATURE: /* Exchange software "signature" text */
        /* This is optional, but we might later exploit it to achieve
         * server-specific workarounds. */
        if (datalen)
            DB(DB_CMP, "[received: SIGNATURE: '%.*s']\r\n", datalen, data);
        else {
            const char *sig = "picocom v" VERSION_STR;
            r = comport_send_cmd(t, COMPORT_SIGNATURE,
                                        sig, strlen(sig));
            if ( r < 0 ) return -1;
            DB(DB_CMP, "[sent: SIGNATURE: '%s']", sig);
        }
        break;

    case COMPORT_SET_BAUDRATE: /* Remote baud rate update */
        if (datalen >= 4) {
            unsigned int baud = (data[0] << 24) | (data[1] << 16) |
                             (data[2] << 8) | data[3];
            tios_set_baudrate_always(&s->termios, baud);
            DB(DB_CMP, "[received: COMPORT BAUDRATE: %d]\r\n", baud);
        }
        /* XXX the sredird server sends an extra 4-byte value,
         * which looks like the ispeed. It is not in the RFC. */
        s->conf_pending--;
        break;

    case COMPORT_SET_DATASIZE: /* Notification of remote data bit size */
        if (datalen >= 1) {
            int val = 0;
            switch (data[0]) {
            case COMPORT_DATASIZE_5: val = 5; break;
            case COMPORT_DATASIZE_6: val = 6; break;
            case COMPORT_DATASIZE_7: val = 7; break;
            case COMPORT_DATASIZE_8: val = 8; break;
            }
            tios_set_databits(tio, val);
            DB(DB_CMP, "[received: COMPORT DATASIZE: %d]\r\n", val);
        }
        s->conf_pending--;
        break;

    case COMPORT_SET_PARITY: /* Remote parity update */
        if (datalen >= 1) {
            enum parity_e val = P_ERROR;
            switch (data[0]) {
            case COMPORT_PARITY_NONE: val = P_NONE; break;
            case COMPORT_PARITY_ODD:  val = P_ODD; break;
            case COMPORT_PARITY_EVEN: val = P_EVEN; break;
            }
            tios_set_parity(tio, val);
            DB(DB_CMP, "[received: COMPORT PARITY: %s]\r\n", parity_str[val]);

        }
        s->conf_pending--;
        break;

    case COMPORT_SET_STOPSIZE: /* Remote stop bits update */
        if (datalen >= 1) {
            int val = 0;
            switch (data[0]) {
            case COMPORT_STOPSIZE_1: val = 1; break;
            case COMPORT_STOPSIZE_2: val = 2; break;
            }
            tios_set_stopbits(tio, val);
            DB(DB_CMP, "[received: COMPORT STOPSIZE: %d]\r\n", val);
        }
        s->conf_pending--;
        break;

    case COMPORT_SET_CONTROL: /* Remote control state update */
        if (datalen >= 1) {
            switch (data[0]) {
            /* Flow control changes and COMPORT_CONTROL_FC_REQUEST reply */
            case COMPORT_CONTROL_FC_XONOFF:
                tios_set_flowcntrl(tio, FC_XONXOFF);
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: FLOW: %s]\r\n",
                      data[0], flow_str[FC_XONXOFF]);
                s->conf_pending--;
                break;
            case COMPORT_CONTROL_FC_HARDWARE:
                tios_set_flowcntrl(tio, FC_RTSCTS);
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: FLOW: %s]\r\n",
                      data[0], flow_str[FC_RTSCTS]);
                s->conf_pending--;
                break;
            case COMPORT_CONTROL_FC_NONE:
            case COMPORT_CONTROL_FC_DCD:
            case COMPORT_CONTROL_FC_DSR:
                tios_set_flowcntrl(tio, FC_NONE);
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: FLOW: %s]\r\n",
                      data[0], flow_str[FC_NONE]);
                s->conf_pending--;
                break;
            /* DTR changes and COMPORT_CONTROL_DTR_REQUEST reply */
            case COMPORT_CONTROL_DTR_ON:
            case COMPORT_CONTROL_DTR_OFF:
                val = (data[0] == COMPORT_CONTROL_DTR_ON) ? TIOCM_DTR : 0;
                *modem &= ~TIOCM_DTR;
                *modem |= val;
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: dtr=%u]\r\n",
                      data[0], !!val);
                break;
            /* RTS changes and COMPORT_CONTROL_RTS_REQUEST reply */
            case COMPORT_CONTROL_RTS_ON:
            case COMPORT_CONTROL_RTS_OFF:
                val = (data[0] == COMPORT_CONTROL_RTS_ON) ? TIOCM_RTS : 0;
                *modem &= ~TIOCM_RTS;
                *modem |= val;
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: rts=%u]\r\n",
                      data[0], !!val);
                break;
            default:
                DB(DB_CMP, "[received: COMPORT SET_CONTROL: %d: (?)]\r\n", data[0]);
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
            if (data[0] & COMPORT_MODEM_DSR) val |= TIOCM_DSR;
            if (data[0] & COMPORT_MODEM_CTS) val |= TIOCM_CTS;
            DB(DB_CMP, "[received: COMPORT MODEMSTATE: %s]\r\n", repr_modem(val));
            *modem &= ~(TIOCM_CD|TIOCM_RI|TIOCM_DSR|TIOCM_CTS);
            *modem |= val;
        }
        break;

    default:
        if ( DB_ON(DB_CMP) ) {
            int i;
            DB(DB_CMP, "[received: COMPORT CMD=%d:", cmd);
            for ( i = 0; i < datalen; i++ )
                DB(DB_CMP, " %d", data[i]);
            DB(DB_CMP, "]\r\n");
        }
        break;
    }
    return 0;
}


static int
tn2217_init(struct term_s *t)
{
    struct tn2217_state *s;
    int r;

    t->priv = calloc(1, sizeof (struct tn2217_state));
    if ( ! t->priv ) {
        term_errno = TERM_EMEM;
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
    r = opt_will(t, TELOPT_BINARY);
    if ( r < 0 ) return r;
    r = opt_do(t, TELOPT_BINARY);
    if ( r < 0 ) return r;
    r = opt_will(t, TELOPT_SGA);
    if ( r < 0 ) return r;
    r = opt_do(t, TELOPT_SGA);
    if ( r < 0 ) return r;
    r = opt_will(t, TELOPT_COMPORT);
    if ( r < 0 ) return r;

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

    DB(DB_CMP, "[tcgetattr %s]\r\n", repr_termios(&s->termios));
    *termios_out = s->termios;
    return 0;
}

static int
tn2217_tcsetattr(struct term_s *t, int when, const struct termios *tio)
{
    struct tn2217_state *s = STATE(t);
    int rval = 0;

    DB(DB_CMP, "[tcsetattr %s]\r\n", repr_termios(tio));
    s->termios = *tio;
    if (s->can_comport)
        rval = comport_config_port(t, tio);
    else
        s->set_termios = 1;
    return rval;
}

static int
tn2217_modem_get(struct term_s *t, int *modem_out)
{
    struct tn2217_state *s = STATE(t);

    DB(DB_CMP, "[modem_get %s]\r\n", repr_modem(s->modem));
    *modem_out = s->modem;
    return 0;
}

static int
tn2217_modem_bis(struct term_s *t, const int *modem)
{
    struct tn2217_state *s = STATE(t);
    int r;

    DB(DB_CMP, "[modem_bis %s]\r\n", repr_modem(*modem));

    s->modem |= *modem;

    if (s->can_comport) {
        if (*modem & TIOCM_DTR) {
            r = comport_send_set_dtr(t, TIOCM_DTR);
            if (r < 0) return r;
        }
        if (*modem & TIOCM_RTS) {
            r = comport_send_set_rts(t, TIOCM_RTS);
            if (r < 0) return r;
        }
    } else if (*modem & (TIOCM_DTR|TIOCM_RTS))
        s->set_modem = 1;

    return 0;
}

static int
tn2217_modem_bic(struct term_s *t, const int *modem)
{
    struct tn2217_state *s = STATE(t);
    int r;

    DB(DB_CMP, "[modem_bic %s]\r\n", repr_modem(*modem));

    s->modem &= ~*modem;

    if (s->can_comport) {
        if (*modem & TIOCM_DTR) {
            r = comport_send_set_dtr(t, 0);
            if ( r < 0 ) return r;
        }
        if (*modem & TIOCM_RTS) {
            r = comport_send_set_rts(t, 0);
            if ( r < 0 ) return r;
        }
    } else if (*modem & (TIOCM_DTR|TIOCM_RTS))
        s->set_modem = 1;

    return 0;
}

static int
tn2217_send_break(struct term_s *t)
{
    int r;

    /* FIXME(npat): Check can_comport? */
    r = comport_send_cmd1(t, COMPORT_SET_CONTROL,
                          COMPORT_CONTROL_BREAK_ON);
    if ( r < 0 ) return r;
    usleep(250000); /* 250 msec */
    r = comport_send_cmd1(t, COMPORT_SET_CONTROL,
                          COMPORT_CONTROL_BREAK_OFF);
    if ( r < 0 ) return r;

    return 0;
}

static int
tn2217_flush(struct term_s *t, int selector)
{
    unsigned char val;

    /* FIXME(npat): Check can_comport? */
    /* Purge, presumably so that we have something to flush */
    switch (selector) {
    case TCIFLUSH:  val = COMPORT_PURGE_RX; break;
    case TCOFLUSH:  val = COMPORT_PURGE_TX; break;
    default:        val = COMPORT_PURGE_RXTX; break;
    }
    return comport_send_cmd1(t, COMPORT_PURGE_DATA, val);
}

/* Condition: Initial configuration completed. That is: initial
   negotiations completed, configuration commands sent, *and*
   replies received. To be used with wait_cond(). */
static int
cond_initial_conf_complete(struct term_s *t)
{
    struct tn2217_state *s = STATE(t);

    if ( s->configed ) return 1;
    if ( s->can_comport && ! s->conf_pending ) {
        s->configed = 1;
        return 1;
    }
    return 0;
}

/* Condition: Initial negotiations completed, and configuration
   commands sent, but replies not yet received. To be used with
   wait_cond(). */
static int
cond_comport_start(struct term_s *t)
{
    return STATE(t)->can_comport;
}

/* Wait (keep reading from the fd and processing commands) until the
   condition is satisfied (that is, until "cond" returns non-zero), or
   until the timeout expires. Returns a positive on success, zero if
   read(2) returned zero, and a negative on any other error
   (incl. timeoute expiration). On timeout expiration, term_errno is set to
   TERM_ETIMEDOUT.*/
static int
wait_cond(struct term_s *t, int (*cond)(struct term_s *t), int tmo_msec)
{
    struct timeval now, tmo_tv, tmo_abs;
    unsigned char c;
    int r;
    fd_set rdset;

    msec2tv(&tmo_tv, tmo_msec);
    gettimeofday(&now, 0);
    timeradd(&now, &tmo_tv, &tmo_abs);

    while ( ! cond(t) ) {
        FD_ZERO(&rdset);
        FD_SET(t->fd, &rdset);
        r = select(t->fd + 1, &rdset, 0, 0, &tmo_tv);
        if ( r < 0 ) {
            term_errno = TERM_ESELECT; return -1;
        } else if ( r > 0 ) {
            r = read_and_proc(t, &c, 1);
            if ( r == 0 || (r < 0 && term_esys() != EAGAIN) )
                return r;
            /* discard c */
        }
        gettimeofday(&now, 0);
        if ( timercmp(&now, &tmo_abs, <) ) {
            timersub(&tmo_abs, &now, &tmo_tv);
        } else {
            /* Set both term_errno and errno so clients can check either */
            term_errno = TERM_ETIMEDOUT;
            errno = ETIMEDOUT;
            return -1;
        }
    }

    return 1;
}

/* Reads raw binary from the socket and immediately handles any
 * in-stream TELNET commands. */
static int
read_and_proc(struct term_s *t, void *buf, unsigned bufsz)
{
    struct tn2217_state *s = STATE(t);
    unsigned char *in, *out;
    int r, inlen, outlen;
    unsigned char *iac;

    inlen = read(t->fd, buf, bufsz);
    if (inlen <= 0) {
        if ( inlen < 0 ) term_errno = TERM_EINPUT;
        return inlen;
    }

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
                r = recv_cmd_partial(t);
                if ( r < 0 ) return r;
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
         * signal EAGAIN. See the article "Worse is Better".
         */
        term_errno = TERM_EINPUT;
        errno = EAGAIN;
        outlen = -1;
    }

    return outlen;
}

/* Reads raw binary from the socket and immediately handles any
 * in-stream TELNET commands. */
static int
tn2217_read(struct term_s *t, void *buf, unsigned bufsz)
{
    int r;

    /* If s->set_termios is not set (i.e. --noinit was given), the
       port will not be configured and we don't have to wait. */
    if ( STATE(t)->set_termios && ! cond_initial_conf_complete(t) ) {
        /* Port may not have been configured yet. Wait for
           negotiations to end, and configuration commands to get
           transmitted *and* replies received. */
        DBG("tn2217_read WAITING initial_conf_complete\r\n");
        r = wait_cond(t, cond_initial_conf_complete, 5000);
        if ( r <= 0 )
            return r;
        DBG("tn2217_read GOT initial_conf_complete\r\n");
    }

    return read_and_proc(t, buf, bufsz);
}

static int
escape_write(struct term_s *t, const void *buf, unsigned bufsz)
{
    const unsigned char *start, *end;
    unsigned int n, len;

    len = bufsz;
    start = (const unsigned char *)buf;

    /* Double instances of IAC by arranging for overlapping writes. */
    end = (const unsigned char *)memchr(start, IAC, len);
    while (end) {
        n = (end - start) + 1;
        if ( writen_ni(t->fd, start, n) != n ) {
            term_errno = TERM_EOUTPUT; return -1;
        }
        len -= end - start;
        start = end;
        end = (const unsigned char *)memchr(start + 1, IAC, len - 1);
    }

    if ( writen_ni(t->fd, start, len) != len ) {
        term_errno = TERM_EOUTPUT; return -1;
        return -1;
    }
    return bufsz;
}


static int
tn2217_write(struct term_s *t, const void *buf, unsigned bufsz)
{
    int r;

    /* If s->set_termios is not set (i.e. --noinit was given), the
       port will not be configured and we don't have to wait. */
    if ( STATE(t)->set_termios && ! cond_comport_start(t) ) {
        /* Port may not have been configured yet. Wait for
           negotiations to end, and configuration commands to get
           transmitted. It is not necessary to wait for them to get
           replied. */
        DBG("tn2217_write WAITING comport_start\r\n");
        r = wait_cond(t, cond_comport_start, 5000);
        if ( r <= 0 ) {
            if ( r == 0 ) term_errno = TERM_ERDZERO;
            return -1;
        }
        DBG("tn2217_write GOT comport_start\r\n");
    }

    return escape_write(t, buf, bufsz);
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
    .fake_flush = NULL,
    .drain = NULL,
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
