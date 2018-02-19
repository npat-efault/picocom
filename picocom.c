/* vi: set sw=4 ts=4:
 *
 * picocom.c
 *
 * simple dumb-terminal program. Helps you manually configure and test
 * stuff like modems, devices w. serial ports, etc.
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
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <limits.h>
#ifdef USE_FLOCK
#include <sys/file.h>
#endif
#ifdef LINENOISE
#include <dirent.h>
#include <libgen.h>
#endif

#define _GNU_SOURCE
#include <getopt.h>

#include "fdio.h"
#include "split.h"
#include "term.h"
#ifdef LINENOISE
#include "linenoise-1.0/linenoise.h"
#endif

#include "custbaud.h"

/**********************************************************************/

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

/* control-key to printable character (lowcase) */
#define KEYC(k) ((k) | 0x60)
/* printable character to control-key */
#define CKEY(c) ((c) & 0x1f)

#define KEY_EXIT    CKEY('x') /* exit picocom */
#define KEY_QUIT    CKEY('q') /* exit picocom without reseting port */
#define KEY_PULSE   CKEY('p') /* pulse DTR */
#define KEY_TOG_DTR CKEY('t') /* toggle DTR */
#define KEY_TOG_RTS CKEY('g') /* toggle RTS */
#define KEY_BAUD    CKEY('b') /* set baudrate */
#define KEY_BAUD_UP CKEY('u') /* increase baudrate (up) */
#define KEY_BAUD_DN CKEY('d') /* decrase baudrate (down) */
#define KEY_FLOW    CKEY('f') /* change flowcntrl mode */
#define KEY_PARITY  CKEY('y') /* change parity mode */
#define KEY_BITS    CKEY('i') /* change number of databits */
#define KEY_STOP    CKEY('j') /* change number of stopbits */
#define KEY_LECHO   CKEY('c') /* toggle local echo */
#define KEY_STATUS  CKEY('v') /* show program options */
#define KEY_HELP    CKEY('h') /* show help (same as [C-k]) */
#define KEY_KEYS    CKEY('k') /* show available command keys */
#define KEY_SEND    CKEY('s') /* send file */
#define KEY_RECEIVE CKEY('r') /* receive file */
#define KEY_HEX     CKEY('w') /* write hex */
#define KEY_BREAK   CKEY('\\') /* break */

/**********************************************************************/

/* implemented caracter mappings */
#define M_CRLF    (1 << 0)  /* map CR  --> LF */
#define M_CRCRLF  (1 << 1)  /* map CR  --> CR + LF */
#define M_IGNCR   (1 << 2)  /* map CR  --> <nothing> */
#define M_LFCR    (1 << 3)  /* map LF  --> CR */
#define M_LFCRLF  (1 << 4)  /* map LF  --> CR + LF */
#define M_IGNLF   (1 << 5)  /* map LF  --> <nothing> */
#define M_DELBS   (1 << 6)  /* map DEL --> BS */
#define M_BSDEL   (1 << 7)  /* map BS  --> DEL */
#define M_SPCHEX  (1 << 8)  /* map special chars --> hex */
#define M_TABHEX  (1 << 9)  /* map TAB --> hex */
#define M_CRHEX   (1 << 10)  /* map CR --> hex */
#define M_LFHEX   (1 << 11) /* map LF --> hex */
#define M_8BITHEX (1 << 12) /* map 8-bit chars --> hex */
#define M_NRMHEX  (1 << 13) /* map normal ascii chars --> hex */
#define M_NFLAGS 14

/* default character mappings */
#define M_I_DFL 0
#define M_O_DFL 0
#define M_E_DFL (M_DELBS | M_CRCRLF)

/* character mapping names */
struct map_names_s {
    const char *name;
    int flag;
} map_names[] = {
    { "crlf", M_CRLF },
    { "crcrlf", M_CRCRLF },
    { "igncr", M_IGNCR },
    { "lfcr", M_LFCR },
    { "lfcrlf", M_LFCRLF },
    { "ignlf", M_IGNLF },
    { "delbs", M_DELBS },
    { "bsdel", M_BSDEL },
    { "spchex", M_SPCHEX },
    { "tabhex", M_TABHEX },
    { "crhex", M_CRHEX },
    { "lfhex", M_LFHEX },
    { "8bithex", M_8BITHEX },
    { "nrmhex", M_NRMHEX },
    /* Sentinel */
    { NULL, 0 }
};

int
parse_map (char *s)
{
    const char *m;
    char *t;
    int f, flags, i;

    flags = 0;
    while ( (t = strtok(s, ", \t")) ) {
        for (i=0; (m = map_names[i].name); i++) {
            if ( ! strcmp(t, m) ) {
                f = map_names[i].flag;
                break;
            }
        }
        if ( m ) flags |= f;
        else { flags = -1; break; }
        s = NULL;
    }

    return flags;
}

void
print_map (int flags)
{
    int i;

    for (i = 0; i < M_NFLAGS; i++)
        if ( flags & (1 << i) )
            printf("%s,", map_names[i].name);
    printf("\n");
}

/**********************************************************************/

struct {
    char *port;
    int baud;
    enum flowcntrl_e flow;
    enum parity_e parity;
    int databits;
    int stopbits;
    int lecho;
    int noinit;
    int noreset;
    int hangup;
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
    int nolock;
#endif
    unsigned char escape;
    int noescape;
    char send_cmd[128];
    char receive_cmd[128];
    int imap;
    int omap;
    int emap;
    char *log_filename;
    char *initstring;
    int exit_after;
    int exit;
    int lower_rts;
    int lower_dtr;
    int raise_rts;
    int raise_dtr;
    int quiet;
} opts = {
    .port = NULL,
    .baud = 9600,
    .flow = FC_NONE,
    .parity = P_NONE,
    .databits = 8,
    .stopbits = 1,
    .lecho = 0,
    .noinit = 0,
    .noreset = 0,
    .hangup = 0,
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
    .nolock = 0,
#endif
    .escape = CKEY('a'),
    .noescape = 0,
    .send_cmd = "sz -vv",
    .receive_cmd = "rz -vv -E",
    .imap = M_I_DFL,
    .omap = M_O_DFL,
    .emap = M_E_DFL,
    .log_filename = NULL,
    .initstring = NULL,
    .exit_after = -1,
    .exit = 0,
    .lower_rts = 0,
    .lower_dtr = 0,
    .raise_rts = 0,
    .raise_dtr = 0,
    .quiet = 0
};

int sig_exit = 0;

#define STI STDIN_FILENO
#define STO STDOUT_FILENO
#define STE STDERR_FILENO

int tty_fd = -1;
int log_fd = -1;

/* RTS and DTR are usually raised upon opening the serial port (at least
   as tested on Linux, OpenBSD and macOS, but FreeBSD behave different) */
int rts_up = 1;
int dtr_up = 1;

#define TTY_Q_SZ_MIN 256
#ifndef TTY_Q_SZ
#define TTY_Q_SZ 32768
#endif

struct tty_q {
    int sz;
    int len;
    unsigned char *buff;
} tty_q = {
    .sz = 0,
    .len = 0,
    .buff = NULL
};

#define STI_RD_SZ 16
#define TTY_RD_SZ 128

int tty_write_sz;

#define TTY_WRITE_SZ_DIV 10
#define TTY_WRITE_SZ_MIN 8

#define set_tty_write_sz(baud)                          \
    do {                                                \
        tty_write_sz = (baud) / TTY_WRITE_SZ_DIV;       \
        if ( tty_write_sz < TTY_WRITE_SZ_MIN )          \
            tty_write_sz = TTY_WRITE_SZ_MIN;            \
    } while (0)

/**********************************************************************/

#ifdef UUCP_LOCK_DIR

/* use HDB UUCP locks  .. see
 * <http://www.faqs.org/faqs/uucp-internals> for details
 */

char lockname[_POSIX_PATH_MAX] = "";

int
uucp_lockname(const char *dir, const char *file)
{
    char *p, *cp;
    struct stat sb;

    if ( ! dir || *dir == '\0' || stat(dir, &sb) != 0 )
        return -1;

    /* cut-off initial "/dev/" from file-name */
    p = strchr(file + 1, '/');
    p = p ? p + 1 : (char *)file;
    /* replace '/'s with '_'s in what remains (after making a copy) */
    p = cp = strdup(p);
    do { if ( *p == '/' ) *p = '_'; } while(*p++);
    /* build lockname */
    snprintf(lockname, sizeof(lockname), "%s/LCK..%s", dir, cp);
    /* destroy the copy */
    free(cp);

    return 0;
}

int
uucp_lock(void)
{
    int r, fd, pid;
    char buf[16];
    mode_t m;

    if ( lockname[0] == '\0' ) return 0;

    fd = open(lockname, O_RDONLY);
    if ( fd >= 0 ) {
        r = read(fd, buf, sizeof(buf));
        close(fd);
        /* if r == 4, lock file is binary (old-style) */
        pid = (r == 4) ? *(int *)buf : strtol(buf, NULL, 10);
        if ( pid > 0
             && kill((pid_t)pid, 0) < 0
             && errno == ESRCH ) {
            /* stale lock file */
            pinfo("\r\nRemoving stale lock: %s\r\n", lockname);
            sleep(1);
            unlink(lockname);
        } else {
            lockname[0] = '\0';
            errno = EEXIST;
            return -1;
        }
    }
    /* lock it */
    m = umask(022);
    fd = open(lockname, O_WRONLY|O_CREAT|O_EXCL, 0666);
    if ( fd < 0 ) { lockname[0] = '\0'; return -1; }
    umask(m);
    snprintf(buf, sizeof(buf), "%04d\n", getpid());
    write(fd, buf, strlen(buf));
    close(fd);

    return 0;
}

int
uucp_unlock(void)
{
    if ( lockname[0] ) unlink(lockname);
    return 0;
}

#endif /* of UUCP_LOCK_DIR */

/**********************************************************************/

#define HEXBUF_SZ 128
#define HEXDELIM " \r;:-_.,/"

#define hexisdelim(c) ( strchr(HEXDELIM, (c)) != NULL )

static inline int
hex2byte (char c)
{
    int r;

    if ( c >= '0' && c <= '9' )
        r = c - '0';
    else if ( c >= 'A' && c <= 'F')
        r = c - 'A' + 10;
    else if ( c >= 'a' && c <= 'f' )
        r = c - 'a' + 10;
    else
        r = -1;

    return r;
}

int
hex2bin(unsigned char *buf, int sz, const char *str)
{
    char c;
    int b0, b1;
    int i;

    i = 0;
    while (i < sz) {
        /* delimiter, end of string, or high nibble */
        c = *str++;
        if ( c == '\0' ) break;
        if ( hexisdelim(c) ) continue;
        b0 = hex2byte(c);
        if ( b0 < 0 ) return -1;
        /* low nibble */
        c = *str++;
        if ( c == '\0' ) return -1;
        b1 = hex2byte(c);
        if ( b1 < 0 ) return -1;
        /* pack byte */
        buf[i++] = (unsigned char)b0 << 4 | (unsigned char)b1;
    }

    return i;
}

/**********************************************************************/

#ifndef LINENOISE

char *
read_filename (void)
{
    char fname[_POSIX_PATH_MAX];
    int r;

    fd_printf(STO, "\r\n*** file: ");
    r = fd_readline(STI, STO, fname, sizeof(fname));
    fd_printf(STO, "\r\n");
    if ( r < 0 )
        return NULL;
    else
        return strdup(fname);
}

int
read_baud (void)
{
    char baudstr[9], *ep;
    int baud = -1, r;

    do {
        fd_printf(STO, "\r\n*** baud: ");
        r = fd_readline(STI, STO, baudstr, sizeof(baudstr));
        fd_printf(STO, "\r\n");
        if ( r < 0 )
            break;
        baud = strtol(baudstr, &ep, 0);
        if ( ! ep || *ep != '\0' || ! term_baud_ok(baud) || baud == 0 ) {
            fd_printf(STO, "*** Invalid baudrate!");
            baud = -1;
        }
    } while (baud < 0);

    return baud;
}

int
read_hex (unsigned char *buff, int sz)
{
    char hexstr[256];
    int r, n;

    do {
        fd_printf(STO, "\r\n*** hex: ");
        r = fd_readline(STI, STO, hexstr, sizeof(hexstr));
        fd_printf(STO, "\r\n");
        if ( r < 0 ) {
            n = 0;
            break;
        }
        n = hex2bin(buff, sz, hexstr);
        if ( n < 0 )
            fd_printf(STO, "*** Invalid hex!");
    } while (n < 0);

    return n;
}

#else /* LINENOISE defined */

void
file_completion_cb (const char *buf, linenoiseCompletions *lc)
{
    DIR *dirp;
    struct dirent *dp;
    char *basec, *basen, *dirc, *dirn;
    int baselen, dirlen, namelen;
    char *fullpath;
    struct stat filestat;

    basec = strdup(buf);
    dirc = strdup(buf);
    dirn = dirname(dirc);
    dirlen = strlen(dirn);
    basen = basename(basec);
    baselen = strlen(basen);
    dirp = opendir(dirn);

    if (dirp) {
        while ((dp = readdir(dirp)) != NULL) {
            namelen = strlen(dp->d_name);
            if (strncmp(basen, dp->d_name, baselen) == 0) {
                /* add 2 extra bytes for possible / in middle & at end */
                fullpath = (char *) malloc(namelen + dirlen + 3);
                memcpy(fullpath, dirn, dirlen + 1);
                if (fullpath[dirlen-1] != '/')
                    strcat(fullpath, "/");
                strncat(fullpath, dp->d_name, namelen);
                if (stat(fullpath, &filestat) == 0) {
                    if (S_ISDIR(filestat.st_mode)) {
                        strcat(fullpath, "/");
                    }
                    linenoiseAddCompletion(lc,fullpath);
                }
                free(fullpath);
            }
        }

        closedir(dirp);
    }
    free(basec);
    free(dirc);
}

static char *history_file_path = NULL;

void
init_history (void)
{
    char *home_directory;
    int home_directory_len;

    home_directory = getenv("HOME");
    if (home_directory) {
        home_directory_len = strlen(home_directory);
        history_file_path = malloc(home_directory_len + 2 + strlen(HISTFILE));
        memcpy(history_file_path, home_directory, home_directory_len + 1);
        if (home_directory[home_directory_len - 1] != '/') {
            strcat(history_file_path, "/");
        }
        strcat(history_file_path, HISTFILE);
        linenoiseHistoryLoad(history_file_path);
    }
}

void
cleanup_history (void)
{
    if (history_file_path)
        free(history_file_path);
}

void
add_history (char *fname)
{
    linenoiseHistoryAdd(fname);
    if (history_file_path)
        linenoiseHistorySave(history_file_path);
}

char *
read_filename (void)
{
    char *fname;
    linenoiseSetCompletionCallback(file_completion_cb);
    fd_printf(STO, "\r\n");
    fname = linenoise("*** file: ");
    fd_printf(STO, "\r");
    linenoiseSetCompletionCallback(NULL);
    if (fname != NULL)
        add_history(fname);
    return fname;
}

int
read_baud (void)
{
    char *baudstr, *ep;
    int baud = -1;

    do {
        fd_printf(STO, "\r\n");
        baudstr = linenoise("*** baud: ");
        fd_printf(STO, "\r");
        if ( baudstr == NULL )
            break;
        baud = strtol(baudstr, &ep, 0);
        if ( ! ep || *ep != '\0' || ! term_baud_ok(baud) || baud == 0 ) {
            fd_printf(STO, "*** Invalid baudrate!");
            baud = -1;
        }
        free(baudstr);
    } while (baud < 0);

    if (baudstr != NULL)
        add_history(baudstr);

    return baud;
}

int
read_hex (unsigned char *buff, int sz)
{
    char *hexstr;
    int n;

    do {
        fd_printf(STO, "\r\n");
        hexstr = linenoise("*** hex: ");
        fd_printf(STO, "\r");
        if ( hexstr == NULL ) {
            n = 0;
            break;
        }
        n = hex2bin(buff, sz, hexstr);
        if ( n < 0 )
            fd_printf(STO, "*** Invalid hex!");
        free(hexstr);
    } while (n < 0);

    return n;
}

#endif /* of ifndef LINENOISE */

/**********************************************************************/

int
pinfo(const char *format, ...)
{
    va_list args;
    int len;

    if ( opts.quiet ) {
        return 0;
    }
    va_start(args, format);
    len = fd_vprintf(STO, format, args);
    va_end(args);

    return len;
}

void
cleanup (int drain, int noreset, int hup)
{
    if ( tty_fd >= 0 ) {
        /* Print msg if they fail? Can't do anything, anyway... */
        if ( drain )
            term_drain(tty_fd);
        term_flush(tty_fd);
        /* term_flush does not work with some drivers. If we try to
           drain or even close the port while there are still data in
           it's output buffers *and* flow-control is enabled we may
           block forever. So we "fake" a flush, by temporarily setting
           f/c to none, waiting for any data in the output buffer to
           drain, and then reseting f/c to it's original setting. If
           the real flush above does works, then the fake one should
           amount to instantaneously switching f/c to none and then
           back to its propper setting. */
        if ( opts.flow != FC_NONE ) term_fake_flush(tty_fd);
        term_set_hupcl(tty_fd, !noreset || hup);
        term_apply(tty_fd, 1);
        if ( noreset ) {
            pinfo("Skipping tty reset...\r\n");
            term_erase(tty_fd);
#ifdef USE_FLOCK
            /* Explicitly unlock tty_fd before exiting. See
               comments in term.c/term_exitfunc() for more. */
            flock(tty_fd, LOCK_UN);
#endif
            close(tty_fd);
            tty_fd = -1;
        }
    }

#ifdef LINENOISE
    cleanup_history();
#endif
#ifdef UUCP_LOCK_DIR
    uucp_unlock();
#endif
    if ( opts.initstring ) {
        free(opts.initstring);
        opts.initstring = NULL;
    }
    if ( tty_q.buff ) {
        free(tty_q.buff);
        tty_q.buff = NULL;
    }
    free(opts.port);
    if (opts.log_filename) {
        free(opts.log_filename);
        close(log_fd);
    }
}

void
fatal (const char *format, ...)
{
    va_list args;

    fd_printf(STE, "\r\nFATAL: ");
    va_start(args, format);
    fd_vprintf(STE, format, args);
    va_end(args);
    fd_printf(STE, "\r\n");

    cleanup(0 /* drain */, opts.noreset, opts.hangup);

    exit(EXIT_FAILURE);
}

/**********************************************************************/

/* maximum number of chars that can replace a single characted
   due to mapping */
#define M_MAXMAP 4

int
map2hex (char *b, char c)
{
    const char *hexd = "0123456789abcdef";

    b[0] = '[';
    b[1] = hexd[(unsigned char)c >> 4];
    b[2] = hexd[(unsigned char)c & 0x0f];
    b[3] = ']';
    return 4;
}

int
do_map (char *b, int map, char c)
{
    int n = -1;

    switch (c) {
    case '\x7f':
        /* DEL mapings */
        if ( map & M_DELBS ) {
            b[0] = '\x08'; n = 1;
        }
        break;
    case '\x08':
        /* BS mapings */
        if ( map & M_BSDEL ) {
            b[0] = '\x7f'; n = 1;
        }
        break;
    case '\x0d':
        /* CR mappings */
        if ( map & M_CRLF ) {
            b[0] = '\x0a'; n = 1;
        } else if ( map & M_CRCRLF ) {
            b[0] = '\x0d'; b[1] = '\x0a'; n = 2;
        } else if ( map & M_IGNCR ) {
            n = 0;
        } else if ( map & M_CRHEX ) {
            n = map2hex(b, c);
        }
        break;
    case '\x0a':
        /* LF mappings */
        if ( map & M_LFCR ) {
            b[0] = '\x0d'; n = 1;
        } else if ( map & M_LFCRLF ) {
            b[0] = '\x0d'; b[1] = '\x0a'; n = 2;
        } else if ( map & M_IGNLF ) {
            n = 0;
        } else if ( map & M_LFHEX ) {
            n = map2hex(b, c);
        }
        break;
    case '\x09':
        /* TAB mappings */
        if ( map & M_TABHEX ) {
            n = map2hex(b,c);
        }
        break;
    default:
        break;
    }

    if ( n < 0 && map & M_SPCHEX ) {
        if ( c == '\x7f' || ( (unsigned char)c < 0x20
                              && c != '\x09' && c != '\x0a'
                              && c != '\x0d') ) {
            n = map2hex(b,c);
        }
    }
    if ( n < 0 && map & M_8BITHEX ) {
        if ( c & 0x80 ) {
            n = map2hex(b,c);
        }
    }
    if ( n < 0 && map & M_NRMHEX ) {
        if ( (unsigned char)c >= 0x20 && (unsigned char)c < 0x7f ) {
            n = map2hex(b,c);
        }
    }
    if ( n < 0 ) {
        b[0] = c; n = 1;
    }

    assert(n > 0 && n <= M_MAXMAP);

    return n;
}

void
map_and_write (int fd, int map, char c)
{
    char b[M_MAXMAP];
    int n;

    n = do_map(b, map, c);
    if ( n )
        if ( writen_ni(fd, b, n) < n )
            fatal("write to stdout failed: %s", strerror(errno));
}

/**********************************************************************/

int
baud_up (int baud)
{
    return term_baud_up(baud);
}

int
baud_down (int baud)
{
    int nb;
    nb = term_baud_down(baud);
    if (nb == 0)
        nb = baud;
    return nb;
}

enum flowcntrl_e
flow_next (enum flowcntrl_e flow)
{
    switch(flow) {
    case FC_NONE:
        flow = FC_RTSCTS;
        break;
    case FC_RTSCTS:
        flow = FC_XONXOFF;
        break;
    case FC_XONXOFF:
        flow = FC_NONE;
        break;
    default:
        flow = FC_NONE;
        break;
    }

    return flow;
}

enum parity_e
parity_next (enum parity_e parity)
{
    switch(parity) {
    case P_NONE:
        parity = P_EVEN;
        break;
    case P_EVEN:
        parity = P_ODD;
        break;
    case P_ODD:
        parity = P_NONE;
        break;
    default:
        parity = P_NONE;
        break;
    }

    return parity;
}

int
bits_next (int bits)
{
    bits++;
    if (bits > 8) bits = 5;

    return bits;
}

int
stopbits_next (int bits)
{
    bits++;
    if (bits > 2) bits = 1;

    return bits;
}

/**********************************************************************/

#define statpf(...) \
    do { if (! quiet) fd_printf(__VA_ARGS__); } while(0)

int
show_status (int quiet)
{
    int baud, bits, stopbits, mctl;
    enum flowcntrl_e flow;
    enum parity_e parity;
    int mismatch = 0;

    term_refresh(tty_fd);

    baud = term_get_baudrate(tty_fd, NULL);
    flow = term_get_flowcntrl(tty_fd);
    parity = term_get_parity(tty_fd);
    bits = term_get_databits(tty_fd);
    stopbits = term_get_stopbits(tty_fd);

    statpf(STO, "\r\n");

    if ( baud != opts.baud ) {
        mismatch++;
        statpf(STO, "*** baud: %d (%d)\r\n", opts.baud, baud);
    } else {
        statpf(STO, "*** baud: %d\r\n", opts.baud);
    }
    if ( flow != opts.flow ) {
        mismatch++;
        statpf(STO, "*** flow: %s (%s)\r\n",
                  flow_str[opts.flow], flow_str[flow]);
    } else {
        statpf(STO, "*** flow: %s\r\n", flow_str[opts.flow]);
    }
    if ( parity != opts.parity ) {
        mismatch++;
        statpf(STO, "*** parity: %s (%s)\r\n",
                  parity_str[opts.parity], parity_str[parity]);
    } else {
        statpf(STO, "*** parity: %s\r\n", parity_str[opts.parity]);
    }
    if ( bits != opts.databits ) {
        mismatch++;
        statpf(STO, "*** databits: %d (%d)\r\n", opts.databits, bits);
    } else {
        statpf(STO, "*** databits: %d\r\n", opts.databits);
    }
    if ( stopbits != opts.stopbits ) {
        mismatch++;
        statpf(STO, "*** stopbits: %d (%d)\r\n", opts.stopbits, stopbits);
    } else {
        statpf(STO, "*** stopbits: %d\r\n", opts.stopbits);
    }

    mctl = term_get_mctl(tty_fd);
    if (mctl >= 0 && mctl != MCTL_UNAVAIL) {
        if ( ((mctl & MCTL_DTR) ? 1 : 0) == dtr_up ) {
            statpf(STO, "*** dtr: %s\r\n", dtr_up ? "up" : "down");
        } else {
            mismatch++;
            statpf(STO, "*** dtr: %s (%s)\r\n",
                   dtr_up ? "up" : "down",
                   (mctl & MCTL_DTR) ? "up" : "down");
        }
        if ( ((mctl & MCTL_RTS) ? 1 : 0) == rts_up ) {
            statpf(STO, "*** rts: %s\r\n", rts_up ? "up" : "down");
        } else {
            mismatch++;
            statpf(STO, "*** rts: %s (%s)\r\n",
                   rts_up ? "up" : "down",
                   (mctl & MCTL_RTS) ? "up" : "down");
        }
        statpf(STO, "*** mctl: ");
        statpf(STO, "DTR:%c DSR:%c DCD:%c RTS:%c CTS:%c RI:%c\r\n",
               (mctl & MCTL_DTR) ? '1' : '0',
               (mctl & MCTL_DSR) ? '1' : '0',
               (mctl & MCTL_DCD) ? '1' : '0',
               (mctl & MCTL_RTS) ? '1' : '0',
               (mctl & MCTL_CTS) ? '1' : '0',
               (mctl & MCTL_RI) ? '1' : '0');
    } else {
        statpf(STO, "*** dtr: %s\r\n", dtr_up ? "up" : "down");
        statpf(STO, "*** rts: %s\r\n", rts_up ? "up" : "down");
    }

    return mismatch;
}

#undef statpf

/**********************************************************************/

void
show_keys()
{
#ifndef NO_HELP
    fd_printf(STO, "\r\n");
    fd_printf(STO, "*** Picocom commands (all prefixed by [C-%c])\r\n",
              KEYC(opts.escape));
    fd_printf(STO, "\r\n");
    fd_printf(STO, "*** [C-%c] : Exit picocom\r\n",
              KEYC(KEY_EXIT));
    fd_printf(STO, "*** [C-%c] : Exit without reseting serial port\r\n",
              KEYC(KEY_QUIT));
    fd_printf(STO, "*** [C-%c] : Set baudrate\r\n",
              KEYC(KEY_BAUD));
    fd_printf(STO, "*** [C-%c] : Increase baudrate (baud-up)\r\n",
              KEYC(KEY_BAUD_UP));
    fd_printf(STO, "*** [C-%c] : Decrease baudrate (baud-down)\r\n",
              KEYC(KEY_BAUD_DN));;
    fd_printf(STO, "*** [C-%c] : Change number of databits\r\n",
              KEYC(KEY_BITS));
    fd_printf(STO, "*** [C-%c] : Change number of stopbits\r\n",
              KEYC(KEY_STOP));
    fd_printf(STO, "*** [C-%c] : Change flow-control mode\r\n",
              KEYC(KEY_FLOW));
    fd_printf(STO, "*** [C-%c] : Change parity mode\r\n",
              KEYC(KEY_PARITY));
    fd_printf(STO, "*** [C-%c] : Pulse DTR\r\n",
              KEYC(KEY_PULSE));
    fd_printf(STO, "*** [C-%c] : Toggle DTR\r\n",
              KEYC(KEY_TOG_DTR));
    fd_printf(STO, "*** [C-%c] : Toggle RTS\r\n",
              KEYC(KEY_TOG_RTS));
    fd_printf(STO, "*** [C-%c] : Send break\r\n",
              KEYC(KEY_BREAK));
    fd_printf(STO, "*** [C-%c] : Toggle local echo\r\n",
              KEYC(KEY_LECHO));
    fd_printf(STO, "*** [C-%c] : Write hex\r\n",
              KEYC(KEY_HEX));
    fd_printf(STO, "*** [C-%c] : Send file\r\n",
              KEYC(KEY_SEND));
    fd_printf(STO, "*** [C-%c] : Receive file\r\n",
              KEYC(KEY_RECEIVE));
    fd_printf(STO, "*** [C-%c] : Show port settings\r\n",
              KEYC(KEY_STATUS));
    fd_printf(STO, "*** [C-%c] : Show this message\r\n",
              KEYC(KEY_HELP));
    fd_printf(STO, "\r\n");
#else /* defined NO_HELP */
    fd_printf(STO, "*** Help is disabled.\r\n");
#endif /* of NO_HELP */
}

/**********************************************************************/

#define RUNCMD_ARGS_MAX 32
#define RUNCMD_EXEC_FAIL 126

void
establish_child_signal_handlers (void)
{
    struct sigaction dfl_action;

    /* Set up the structure to specify the default action. */
    dfl_action.sa_handler = SIG_DFL;
    sigemptyset (&dfl_action.sa_mask);
    dfl_action.sa_flags = 0;

    sigaction (SIGINT, &dfl_action, NULL);
    sigaction (SIGTERM, &dfl_action, NULL);
}

int
run_cmd(int fd, const char *cmd, const char *args_extra)
{
    pid_t pid;
    sigset_t sigm, sigm_old;
    struct sigaction ign_action, old_action;

    /* Picocom ignores SIGINT while the command is running */
    ign_action.sa_handler = SIG_IGN;
    sigemptyset (&ign_action.sa_mask);
    ign_action.sa_flags = 0;
    sigaction (SIGINT, &ign_action, &old_action);
    /* block signals, let child establish its own handlers */
    sigemptyset(&sigm);
    sigaddset(&sigm, SIGTERM);
    sigaddset(&sigm, SIGINT);
    sigprocmask(SIG_BLOCK, &sigm, &sigm_old);

    pid = fork();
    if ( pid < 0 ) {
        sigprocmask(SIG_SETMASK, &sigm_old, NULL);
        fd_printf(STO, "*** cannot fork: %s ***\r\n", strerror(errno));
        return -1;
    } else if ( pid ) {
        /* father: picocom */
        int status, r;

        /* reset the mask */
        sigprocmask(SIG_SETMASK, &sigm_old, NULL);
        /* wait for child to finish */
        do {
            r = waitpid(pid, &status, 0);
        } while ( r < 0 && errno == EINTR );
        /* reset terminal (back to raw mode) */
        term_apply(STI, 0);
        /* re-enable SIGINT */
        sigaction(SIGINT, &old_action, NULL);
        /* check and report child return status */
        if ( WIFEXITED(status) ) {
            fd_printf(STO, "\r\n*** exit status: %d ***\r\n",
                      WEXITSTATUS(status));
            return WEXITSTATUS(status);
        } else if ( WIFSIGNALED(status) ) {
            fd_printf(STO, "\r\n*** killed by signal: %d ***\r\n",
                      WTERMSIG(status));
            return -1;
        } else {
            fd_printf(STO, "\r\n*** abnormal termination: 0x%x ***\r\n", r);
            return -1;
        }
    } else {
        /* child: external program */
        long fl;
        int argc;
        char *argv[RUNCMD_ARGS_MAX + 1];
        int r;

        /* unmanage terminal, and reset it to canonical mode */
        term_drain(STI);
        term_remove(STI);
        /* unmanage serial port fd, without reset */
        term_erase(fd);
        /* set serial port fd to blocking mode */
        fl = fcntl(fd, F_GETFL);
        fl &= ~O_NONBLOCK;
        fcntl(fd, F_SETFL, fl);
        /* connect stdin and stdout to serial port */
        close(STI);
        close(STO);
        dup2(fd, STI);
        dup2(fd, STO);

        /* build command arguments vector */
        argc = 0;
        r = split_quoted(cmd, &argc, argv, RUNCMD_ARGS_MAX);
        if ( r < 0 ) {
            fd_printf(STE, "Cannot parse command\n");
            exit(RUNCMD_EXEC_FAIL);
        }
        r = split_quoted(args_extra, &argc, argv, RUNCMD_ARGS_MAX);
        if ( r < 0 ) {
            fd_printf(STE, "Cannot parse extra args\n");
            exit(RUNCMD_EXEC_FAIL);
        }
        if ( argc < 1 ) {
            fd_printf(STE, "No command given\n");
            exit(RUNCMD_EXEC_FAIL);
        }
        argv[argc] = NULL;

        /* run extenral command */
        fd_printf(STE, "$ %s %s\n", cmd, args_extra);
        establish_child_signal_handlers();
        sigprocmask(SIG_SETMASK, &sigm_old, NULL);
        execvp(argv[0], argv);

        fd_printf(STE, "exec: %s\n", strerror(errno));
        exit(RUNCMD_EXEC_FAIL);
    }
}

/**********************************************************************/

int tty_q_push(const char *s, int len) {
    int i, sz, n;
    unsigned char *b;

    for (i = 0; i < len; i++) {
        while (tty_q.len + M_MAXMAP > tty_q.sz) {
            sz = tty_q.sz * 2;
            if ( TTY_Q_SZ && sz > TTY_Q_SZ )
                return i;
            b = realloc(tty_q.buff, sz);
            if ( ! b )
                return i;
            tty_q.buff = b;
            tty_q.sz = sz;
#if 0
            fd_printf(STO, "New tty_q size: %d\r\n", sz);
#endif
        }
        n = do_map((char *)tty_q.buff + tty_q.len,
                   opts.omap, s[i]);
        tty_q.len += n;
        /* write to STO if local-echo is enabled */
        if ( opts.lecho )
            map_and_write(STO, opts.emap, s[i]);
    }

    return i;
}

/* Process command key. Returns non-zero if command results in picocom
   exit, zero otherwise. */
int
do_command (unsigned char c)
{
    int newbaud, newbits, newstopbits;
    enum flowcntrl_e newflow;
    enum parity_e newparity;
    const char *xfr_cmd;
    char *fname;
    unsigned char hexbuf[HEXBUF_SZ];
    int n, r;

    switch (c) {
    case KEY_EXIT:
        return 1;
    case KEY_QUIT:
        opts.noreset = 1;
        return 1;
    case KEY_STATUS:
        show_status(0);
        break;
    case KEY_HELP:
    case KEY_KEYS:
        show_keys();
        break;
    case KEY_PULSE:
        fd_printf(STO, "\r\n*** pulse DTR ***\r\n");
        if ( term_pulse_dtr(tty_fd) < 0 )
            fd_printf(STO, "*** FAILED\r\n");
        else
            dtr_up = 1;
        break;
    case KEY_TOG_DTR:
        if ( dtr_up )
            r = term_lower_dtr(tty_fd);
        else
            r = term_raise_dtr(tty_fd);
        if ( r >= 0 ) dtr_up = ! dtr_up;
        fd_printf(STO, "\r\n*** DTR: %s ***\r\n",
                  dtr_up ? "up" : "down");
        break;
    case KEY_TOG_RTS:
        if ( rts_up )
            r = term_lower_rts(tty_fd);
        else
            r = term_raise_rts(tty_fd);
        if ( r >= 0 ) rts_up = ! rts_up;
        fd_printf(STO, "\r\n*** RTS: %s ***\r\n",
                  rts_up ? "up" : "down");
        break;
    case KEY_BAUD:
    case KEY_BAUD_UP:
    case KEY_BAUD_DN:
        if ( c== KEY_BAUD) {
            newbaud = read_baud();
            if ( newbaud < 0 ) {
                fd_printf(STO, "*** cannot read baudrate ***\r\n");
                break;
            }
            opts.baud = newbaud;
        } else if (c == KEY_BAUD_UP) {
            opts.baud = baud_up(opts.baud);
        } else {
            opts.baud = baud_down(opts.baud);
        }
        term_set_baudrate(tty_fd, opts.baud);
        tty_q.len = 0; term_flush(tty_fd);
        term_apply(tty_fd, 1);
        newbaud = term_get_baudrate(tty_fd, NULL);
        if ( opts.baud != newbaud ) {
            fd_printf(STO, "\r\n*** baud: %d (%d) ***\r\n",
                      opts.baud, newbaud);
        } else {
            fd_printf(STO, "\r\n*** baud: %d ***\r\n", opts.baud);
        }
        set_tty_write_sz(newbaud);
        break;
    case KEY_FLOW:
        opts.flow = flow_next(opts.flow);
        term_set_flowcntrl(tty_fd, opts.flow);
        tty_q.len = 0; term_flush(tty_fd);
        term_apply(tty_fd, 1);
        newflow = term_get_flowcntrl(tty_fd);
        if ( opts.flow != newflow ) {
            fd_printf(STO, "\r\n*** flow: %s (%s) ***\r\n",
                      flow_str[opts.flow], flow_str[newflow]);
        } else {
            fd_printf(STO, "\r\n*** flow: %s ***\r\n",
                      flow_str[opts.flow]);
        }
        break;
    case KEY_PARITY:
        opts.parity = parity_next(opts.parity);
        term_set_parity(tty_fd, opts.parity);
        tty_q.len = 0; term_flush(tty_fd);
        term_apply(tty_fd, 1);
        newparity = term_get_parity(tty_fd);
        if (opts.parity != newparity ) {
            fd_printf(STO, "\r\n*** parity: %s (%s) ***\r\n",
                      parity_str[opts.parity],
                      parity_str[newparity]);
        } else {
            fd_printf(STO, "\r\n*** parity: %s ***\r\n",
                      parity_str[opts.parity]);
        }
        break;
    case KEY_BITS:
        opts.databits = bits_next(opts.databits);
        term_set_databits(tty_fd, opts.databits);
        tty_q.len = 0; term_flush(tty_fd);
        term_apply(tty_fd, 1);
        newbits = term_get_databits(tty_fd);
        if (opts.databits != newbits ) {
            fd_printf(STO, "\r\n*** databits: %d (%d) ***\r\n",
                      opts.databits, newbits);
        } else {
            fd_printf(STO, "\r\n*** databits: %d ***\r\n",
                      opts.databits);
        }
        break;
    case KEY_STOP:
        opts.stopbits = stopbits_next(opts.stopbits);
        term_set_stopbits(tty_fd, opts.stopbits);
        tty_q.len = 0; term_flush(tty_fd);
        term_apply(tty_fd, 1);
        newstopbits = term_get_stopbits(tty_fd);
        if (opts.stopbits != newstopbits ) {
            fd_printf(STO, "\r\n*** stopbits: %d (%d) ***\r\n",
                      opts.stopbits, newstopbits);
        } else {
            fd_printf(STO, "\r\n*** stopbits: %d ***\r\n",
                      opts.stopbits);
        }
        break;
    case KEY_LECHO:
        opts.lecho = ! opts.lecho;
        fd_printf(STO, "\r\n*** local echo: %s ***\r\n",
                  opts.lecho ? "yes" : "no");
        break;
    case KEY_SEND:
    case KEY_RECEIVE:
        xfr_cmd = (c == KEY_SEND) ? opts.send_cmd : opts.receive_cmd;
        if ( xfr_cmd[0] == '\0' ) {
            fd_printf(STO, "\r\n*** command disabled ***\r\n");
            break;
        }
        fname = read_filename();
        if (fname == NULL) {
            fd_printf(STO, "*** cannot read filename ***\r\n");
            break;
        }
        run_cmd(tty_fd, xfr_cmd, fname);
        free(fname);
        break;
    case KEY_HEX:
        n = read_hex(hexbuf, sizeof(hexbuf));
        if ( n < 0 ) {
            fd_printf(STO, "*** cannot read hex ***\r\n");
            break;
        }
        if ( tty_q_push((char *)hexbuf, n) != n )
            fd_printf(STO, "*** output buffer full ***\r\n");
        fd_printf(STO, "*** wrote %d bytes ***\r\n", n);
        break;
    case KEY_BREAK:
        term_break(tty_fd);
        fd_printf(STO, "\r\n*** break sent ***\r\n");
        break;
    default:
        break;
    }

    return 0;
}

/**********************************************************************/

static struct timeval *
msec2tv (struct timeval *tv, long ms)
{
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;

    return tv;
}

/* loop-exit reason */
enum le_reason {
    LE_CMD,
    LE_IDLE,
    LE_STDIN,
    LE_SIGNAL
};

enum le_reason
loop(void)
{
    enum {
        ST_COMMAND,
        ST_TRANSPARENT
    } state;
    fd_set rdset, wrset;
    int r, n;
    int stdin_closed;

    state = ST_TRANSPARENT;
    if ( ! opts.exit )
        stdin_closed = 0;
    else
        stdin_closed = 1;

    while ( ! sig_exit ) {
        struct timeval tv, *ptv;

        ptv = NULL;
        FD_ZERO(&rdset);
        FD_ZERO(&wrset);
        if ( ! stdin_closed ) FD_SET(STI, &rdset);
        if ( ! opts.exit ) FD_SET(tty_fd, &rdset);
        if ( tty_q.len ) {
            FD_SET(tty_fd, &wrset);
        } else {
            if ( opts.exit_after >= 0 ) {
                msec2tv(&tv, opts.exit_after);
                ptv = &tv;
            } else if ( stdin_closed ) {
                /* stdin closed, output queue empty, and no
                   idle timeout: Exit. */
                return LE_STDIN;
            }
        }

        r = select(tty_fd + 1, &rdset, &wrset, NULL, ptv);
        if ( r < 0 )  {
            if ( errno == EINTR )
                continue;
            else
                fatal("select failed: %d : %s", errno, strerror(errno));
        }
        if ( r == 0 ) {
            /* Idle timeout expired */
            return LE_IDLE;
        }

        if ( FD_ISSET(STI, &rdset) ) {
            /* read from terminal */
            char buff_rd[STI_RD_SZ];
            int i;
            unsigned char c;

            do {
                n = read(STI, buff_rd, sizeof(buff_rd));
            } while (n < 0 && errno == EINTR);
            if (n == 0) {
                stdin_closed = 1;
                pinfo("\r\n** read zero bytes from stdin **\r\n");
                goto skip_proc_STI;
            } else if (n < 0) {
                /* is this really necessary? better safe than sory! */
                if ( errno != EAGAIN && errno != EWOULDBLOCK )
                    fatal("read from stdin failed: %s", strerror(errno));
                else
                    goto skip_proc_STI;
            }

            for ( i = 0; i < n; i++ ) {
                c = buff_rd[i];
                switch (state) {
                case ST_COMMAND:
                    if ( c == opts.escape ) {
                        /* pass the escape character down */
                        if ( tty_q_push((char *)&c, 1) != 1 )
                            fd_printf(STO, "\x07");
                    } else {
                        /* process command key */
                        if ( do_command(c) )
                            /* picocom exit */
                            return LE_CMD;
                    }
                    state = ST_TRANSPARENT;
                    break;
                case ST_TRANSPARENT:
                    if ( ! opts.noescape && c == opts.escape )
                        state = ST_COMMAND;
                    else
                        if ( tty_q_push((char *)&c, 1) != 1 )
                            fd_printf(STO, "\x07");
                    break;
                default:
                    assert(0);
                    break;
                }
            }
        }
    skip_proc_STI:

        if ( FD_ISSET(tty_fd, &rdset) ) {

            char buff_rd[TTY_RD_SZ];
            char buff_map[TTY_RD_SZ * M_MAXMAP];

            /* read from port */

            do {
                n = read(tty_fd, &buff_rd, sizeof(buff_rd));
            } while (n < 0 && errno == EINTR);
            if (n == 0) {
                fatal("read zero bytes from port");
            } else if ( n < 0 ) {
                if ( errno != EAGAIN && errno != EWOULDBLOCK )
                    fatal("read from port failed: %s", strerror(errno));
            } else {
                int i;
                char *bmp = &buff_map[0];
                if ( opts.log_filename )
                    if ( writen_ni(log_fd, buff_rd, n) < n )
                        fatal("write to logfile failed: %s", strerror(errno));
                for (i = 0; i < n; i++) {
                    bmp += do_map(bmp, opts.imap, buff_rd[i]);
                }
                n = bmp - buff_map;
                if ( writen_ni(STO, buff_map, n) < n )
                    fatal("write to stdout failed: %s", strerror(errno));
            }
        }

        if ( FD_ISSET(tty_fd, &wrset) ) {

            /* write to port */

            int sz;
            sz = (tty_q.len < tty_write_sz) ? tty_q.len : tty_write_sz;
            do {
                n = write(tty_fd, tty_q.buff, sz);
            } while ( n < 0 && errno == EINTR );
            if ( n <= 0 )
                fatal("write to port failed: %s", strerror(errno));
            if ( opts.lecho && opts.log_filename )
                if ( writen_ni(log_fd, tty_q.buff, n) < n )
                    fatal("write to logfile failed: %s", strerror(errno));
            memmove(tty_q.buff, tty_q.buff + n, tty_q.len - n);
            tty_q.len -= n;
        }
    }
    return LE_SIGNAL;
}

/**********************************************************************/

void
deadly_handler(int signum)
{
    (void)signum; /* silence unused warning */

    if ( ! sig_exit ) {
        sig_exit = 1;
        kill(0, SIGTERM);
    }
}

void
establish_signal_handlers (void)
{
        struct sigaction exit_action, ign_action;

        /* Set up the structure to specify the exit action. */
        exit_action.sa_handler = deadly_handler;
        sigemptyset (&exit_action.sa_mask);
        exit_action.sa_flags = 0;

        /* Set up the structure to specify the ignore action. */
        ign_action.sa_handler = SIG_IGN;
        sigemptyset (&ign_action.sa_mask);
        ign_action.sa_flags = 0;

        sigaction (SIGTERM, &exit_action, NULL);
        sigaction (SIGINT, &exit_action, NULL);

        sigaction (SIGHUP, &ign_action, NULL);
        sigaction (SIGQUIT, &ign_action, NULL);
        sigaction (SIGALRM, &ign_action, NULL);
        sigaction (SIGUSR1, &ign_action, NULL);
        sigaction (SIGUSR2, &ign_action, NULL);
        sigaction (SIGPIPE, &ign_action, NULL);
}

/**********************************************************************/

void
show_usage(char *name)
{
#ifndef NO_HELP
    char *s;

    s = strrchr(name, '/');
    s = s ? s+1 : name;

    printf("picocom v%s\n", VERSION_STR);

    printf("\nCompiled-in options:\n");
    printf("  TTY_Q_SZ is %d\n", TTY_Q_SZ);
#ifdef HIGH_BAUD
    printf("  HIGH_BAUD is enabled\n");
#endif
#ifdef USE_FLOCK
    printf("  USE_FLOCK is enabled\n");
#endif
#ifdef UUCP_LOCK_DIR
    printf("  UUCP_LOCK_DIR is: %s\n", UUCP_LOCK_DIR);
#endif
#ifdef LINENOISE
    printf("  LINENOISE is enabled\n");
    printf("  HISTFILE is: %s\n", HISTFILE);
#endif
#ifdef USE_CUSTOM_BAUD
    printf("  USE_CUSTOM_BAUD is enabled\n");
    if ( ! use_custom_baud() )
        printf("  NO_CUSTOM_BAUD is set\n");
#endif

    printf("\nUsage is: %s [options] <tty port device>\n", s);
    printf("Options are:\n");
    printf("  --<b>aud <baudrate>\n");
    printf("  --<f>low x (=soft,xon/xoff) | h (=hard) | n (=none)\n");
    printf("  --parit<y> o (=odd) | e (=even) | n (=none)\n");
    printf("  --<d>atabits 5 | 6 | 7 | 8\n");
    printf("  --sto<p>bits 1 | 2\n");
    printf("  --<e>scape <char>\n");
    printf("  --<n>o-escape\n");
    printf("  --e<c>ho\n");
    printf("  --no<i>nit\n");
    printf("  --no<r>eset\n");
    printf("  --hang<u>p\n");
    printf("  --no<l>ock\n");
    printf("  --<s>end-cmd <command>\n");
    printf("  --recei<v>e-cmd <command>\n");
    printf("  --imap <map> (input mappings)\n");
    printf("  --omap <map> (output mappings)\n");
    printf("  --emap <map> (local-echo mappings)\n");
    printf("  --lo<g>file <filename>\n");
    printf("  --inits<t>ring <string>\n");
    printf("  --e<x>it-after <msec>\n");
    printf("  --e<X>it\n");
    printf("  --lower-rts\n");
    printf("  --raise-rts\n");
    printf("  --lower-dtr\n");
    printf("  --raise-dtr\n");
    printf("  --<q>uiet\n");
    printf("  --<h>elp\n");
    printf("<map> is a comma-separated list of one or more of:\n");
    printf("  crlf : map CR --> LF\n");
    printf("  crcrlf : map CR --> CR + LF\n");
    printf("  igncr : ignore CR\n");
    printf("  lfcr : map LF --> CR\n");
    printf("  lfcrlf : map LF --> CR + LF\n");
    printf("  ignlf : ignore LF\n");
    printf("  bsdel : map BS --> DEL\n");
    printf("  delbs : map DEL --> BS\n");
    printf("  spchex : map special chars (excl. CR, LF & TAB) --> hex\n");
    printf("  tabhex : map TAB --> hex\n");
    printf("  crhex : map CR --> hex\n");
    printf("  lfhex : map LF --> hex\n");
    printf("  8bithex : map 8-bit chars --> hex\n");
    printf("  nrmhex : map normal ascii chars --> hex\n");
    printf("<?> indicates the equivalent short option.\n");
    printf("Short options are prefixed by \"-\" instead of by \"--\".\n");
#else /* defined NO_HELP */
    printf("Help disabled.\n");
#endif /* of NO_HELP */
    fflush(stdout);
}

/**********************************************************************/

void
parse_args(int argc, char *argv[])
{
    int r;

    static struct option longOptions[] =
    {
        {"receive-cmd", required_argument, 0, 'v'},
        {"send-cmd", required_argument, 0, 's'},
        {"imap", required_argument, 0, 'I' },
        {"omap", required_argument, 0, 'O' },
        {"emap", required_argument, 0, 'E' },
        {"escape", required_argument, 0, 'e'},
        {"no-escape", no_argument, 0, 'n'},
        {"echo", no_argument, 0, 'c'},
        {"noinit", no_argument, 0, 'i'},
        {"noreset", no_argument, 0, 'r'},
        {"hangup", no_argument, 0, 'u'},
        {"nolock", no_argument, 0, 'l'},
        {"flow", required_argument, 0, 'f'},
        {"baud", required_argument, 0, 'b'},
        {"parity", required_argument, 0, 'y'},
        {"databits", required_argument, 0, 'd'},
        {"stopbits", required_argument, 0, 'p'},
        {"logfile", required_argument, 0, 'g'},
        {"initstring", required_argument, 0, 't'},
        {"exit-after", required_argument, 0, 'x'},
        {"exit", no_argument, 0, 'X'},
        {"lower-rts", no_argument, 0, 1},
        {"lower-dtr", no_argument, 0, 2},
        {"raise-rts", no_argument, 0, 3},
        {"raise-dtr", no_argument, 0, 4},
        {"quiet", no_argument, 0, 'q'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    r = 0;
    while (1) {
        int optionIndex = 0;
        int c;
        int map;
        char *ep;

        /* no default error messages printed. */
        opterr = 0;

        c = getopt_long(argc, argv, "hirulcqXnv:s:r:e:f:b:y:d:p:g:t:x:",
                        longOptions, &optionIndex);

        if (c < 0)
            break;

        switch (c) {
        case 's':
            strncpy(opts.send_cmd, optarg, sizeof(opts.send_cmd));
            opts.send_cmd[sizeof(opts.send_cmd) - 1] = '\0';
            break;
        case 'v':
            strncpy(opts.receive_cmd, optarg, sizeof(opts.receive_cmd));
            opts.receive_cmd[sizeof(opts.receive_cmd) - 1] = '\0';
            break;
        case 'I':
            map = parse_map(optarg);
            if (map >= 0) opts.imap = map;
            else { fprintf(stderr, "Invalid --imap\n"); r = -1; }
            break;
        case 'O':
            map = parse_map(optarg);
            if (map >= 0) opts.omap = map;
            else { fprintf(stderr, "Invalid --omap\n"); r = -1; }
            break;
        case 'E':
            map = parse_map(optarg);
            if (map >= 0) opts.emap = map;
            else { fprintf(stderr, "Invalid --emap\n"); r = -1; }
            break;
        case 'c':
            opts.lecho = 1;
            break;
        case 'i':
            opts.noinit = 1;
            break;
        case 'r':
            opts.noreset = 1;
            break;
        case 'u':
            opts.hangup = 1;
            break;
        case 'l':
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
            opts.nolock = 1;
#endif
            break;
        case 'e':
            opts.escape = CKEY(optarg[0]);
            break;
        case 'n':
            opts.noescape = 1;
            break;
        case 'f':
            switch (optarg[0]) {
            case 'X':
            case 'x':
            case 'S':
            case 's':
                opts.flow = FC_XONXOFF;
                break;
            case 'H':
            case 'h':
                opts.flow = FC_RTSCTS;
                break;
            case 'N':
            case 'n':
                opts.flow = FC_NONE;
                break;
            default:
                fprintf(stderr, "Invalid --flow: %c\n", optarg[0]);
                r = -1;
                break;
            }
            break;
        case 'b':
            opts.baud = atoi(optarg);
            if ( opts.baud == 0 || ! term_baud_ok(opts.baud) ) {
                fprintf(stderr, "Invalid --baud: %d\n", opts.baud);
                r = -1;
            }
            break;
        case 'y':
            switch (optarg[0]) {
            case 'e':
                opts.parity = P_EVEN;
                break;
            case 'o':
                opts.parity = P_ODD;
                break;
            case 'n':
                opts.parity = P_NONE;
                break;
            default:
                fprintf(stderr, "Invalid --parity: %c\n", optarg[0]);
                r = -1;
                break;
            }
            break;
        case 'd':
            switch (optarg[0]) {
            case '5':
                opts.databits = 5;
                break;
            case '6':
                opts.databits = 6;
                break;
            case '7':
                opts.databits = 7;
                break;
            case '8':
                opts.databits = 8;
                break;
            default:
                fprintf(stderr, "Invalid --databits: %c\n", optarg[0]);
                r = -1;
                break;
            }
            break;
        case 'p':
            opts.stopbits = 1;
            switch (optarg[0]) {
            case '1':
                break;
            case '2':
                opts.stopbits = 2;
                break;
            /* For backwards compatibility, you can use 'p' to set
               the parity as well */
            case 'e':
                opts.parity = P_EVEN;
                break;
            case 'o':
                opts.parity = P_ODD;
                break;
            case 'n':
                opts.parity = P_NONE;
                break;
            default:
                fprintf(stderr, "Invalid --stopbits: %c\n", optarg[0]);
                r = -1;
                break;
            }
            break;
        case 'g':
            if ( opts.log_filename ) free(opts.log_filename);
            opts.log_filename = strdup(optarg);
            break;
        case 't':
            if ( opts.initstring ) free(opts.initstring);
            opts.initstring = strdup(optarg);
            break;
        case 1:
            opts.lower_rts = 1;
            break;
        case 2:
            opts.lower_dtr = 1;
            break;
        case 3:
            opts.raise_rts = 1;
            break;
        case 4:
            opts.raise_dtr = 1;
            break;
        case 'x':
            opts.exit_after = strtol(optarg, &ep, 10);
            if ( ! ep || *ep != '\0' || opts.exit_after < 0 ) {
                fprintf(stderr, "Inavild --exit-after: %s\n", optarg);
                r = -1;
                break;
            }
            break;
        case 'X':
            opts.exit = 1;
            break;
        case 'q':
            opts.quiet = 1;
            break;
        case 'h':
            show_usage(argv[0]);
            exit(EXIT_SUCCESS);
        case '?':
        default:
            fprintf(stderr, "Unrecognized option(s)\n");
            r = -1;
            break;
        }
        if ( r < 0 ) {
            fprintf(stderr, "Run with '--help'.\n");
            exit(EXIT_FAILURE);
        }
    } /* while */

    if ( opts.raise_rts && opts.lower_rts ) {
        fprintf(stderr, "Both --raise-rts and --lower-rts given\n");
        exit(EXIT_FAILURE);
    }
    if ( opts.raise_dtr && opts.lower_dtr ) {
        fprintf(stderr, "Both --raise-dtr and --lower-dtr given\n");
        exit(EXIT_FAILURE);
    }

    /* --exit overrides --exit-after */
    if ( opts.exit ) opts.exit_after = -1;

    if ( (argc - optind) < 1) {
        fprintf(stderr, "No port given\n");
        fprintf(stderr, "Run with '--help'.\n");
        exit(EXIT_FAILURE);
    }
    opts.port = strdup(argv[argc-1]);
    if ( ! opts.port ) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    if ( opts.quiet )
        return;

#ifndef NO_HELP
    printf("picocom v%s\n", VERSION_STR);
    printf("\n");
    printf("port is        : %s\n", opts.port);
    printf("flowcontrol    : %s\n", flow_str[opts.flow]);
    printf("baudrate is    : %d\n", opts.baud);
    printf("parity is      : %s\n", parity_str[opts.parity]);
    printf("databits are   : %d\n", opts.databits);
    printf("stopbits are   : %d\n", opts.stopbits);
    if ( opts.noescape ) {
        printf("escape is      : none\n");
    } else {
        printf("escape is      : C-%c\n", KEYC(opts.escape));
    }
    printf("local echo is  : %s\n", opts.lecho ? "yes" : "no");
    printf("noinit is      : %s\n", opts.noinit ? "yes" : "no");
    printf("noreset is     : %s\n", opts.noreset ? "yes" : "no");
    printf("hangup is      : %s\n", opts.hangup ? "yes" : "no");
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
    printf("nolock is      : %s\n", opts.nolock ? "yes" : "no");
#endif
    printf("send_cmd is    : %s\n",
           (opts.send_cmd[0] == '\0') ? "disabled" : opts.send_cmd);
    printf("receive_cmd is : %s\n",
           (opts.receive_cmd[0] == '\0') ? "disabled" : opts.receive_cmd);
    printf("imap is        : "); print_map(opts.imap);
    printf("omap is        : "); print_map(opts.omap);
    printf("emap is        : "); print_map(opts.emap);
    printf("logfile is     : %s\n", opts.log_filename ? opts.log_filename : "none");
    if ( opts.initstring ) {
        printf("initstring len : %lu bytes\n",
               (unsigned long)strlen(opts.initstring));
    } else {
        printf("initstring     : none\n");
    }
    if (opts.exit_after < 0) {
        printf("exit_after is  : not set\n");
    } else {
        printf("exit_after is  : %d ms\n", opts.exit_after);
    }
    printf("exit is        : %s\n", opts.exit ? "yes" : "no");
    printf("\n");
    fflush(stdout);
#endif /* of NO_HELP */
}

/**********************************************************************/

void
set_dtr_rts (void)
{
    int r;
    if ( opts.lower_rts ) {
        r = term_lower_rts(tty_fd);
        if ( r < 0 )
            fatal("failed to lower RTS of port: %s",
                  term_strerror(term_errno, errno));
        rts_up = 0;
    } else if ( opts.raise_rts ) {
        r = term_raise_rts(tty_fd);
        if ( r < 0 )
            fatal("failed to raise RTS of port: %s",
                  term_strerror(term_errno, errno));
        rts_up = 1;
    }

    if ( opts.lower_dtr ) {
        r = term_lower_dtr(tty_fd);
        if ( r < 0 )
            fatal("failed to lower DTR of port: %s",
                  term_strerror(term_errno, errno));
        dtr_up = 0;
    } else if ( opts.raise_dtr ) {
        r = term_raise_dtr(tty_fd);
        if ( r < 0 )
            fatal("failed to raise DTR of port: %s",
                  term_strerror(term_errno, errno));
        dtr_up = 1;
    }
    /* Try to read the status of the modem-conrtol lines from the
       port. */
    r = term_get_mctl(tty_fd);
    if ( r >= 0 && r != MCTL_UNAVAIL ) {
        rts_up = (r & MCTL_RTS) != 0;
        dtr_up = (r & MCTL_DTR) != 0;
    }
}


int
main (int argc, char *argv[])
{
    int xcode = EXIT_SUCCESS;
    int ler;
    int r;

    parse_args(argc, argv);

    establish_signal_handlers();

    r = term_lib_init();
    if ( r < 0 )
        fatal("term_lib_init failed: %s", term_strerror(term_errno, errno));

#ifdef UUCP_LOCK_DIR
    if ( ! opts.nolock ) uucp_lockname(UUCP_LOCK_DIR, opts.port);
    if ( uucp_lock() < 0 )
        fatal("cannot lock %s: %s", opts.port, strerror(errno));
#endif

    if (opts.log_filename) {
        log_fd = open(opts.log_filename,
                      O_CREAT | O_RDWR | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (log_fd < 0)
            fatal("cannot open %s: %s", opts.log_filename, strerror(errno));
    }

    tty_fd = open(opts.port, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (tty_fd < 0)
        fatal("cannot open %s: %s", opts.port, strerror(errno));

#ifdef USE_FLOCK
    if ( ! opts.nolock ) {
        r = flock(tty_fd, LOCK_EX | LOCK_NB);
        if ( r < 0 )
            fatal("cannot lock %s: %s", opts.port, strerror(errno));
    }
#endif

    if ( opts.noinit ) {
        r = term_add(tty_fd);
    } else {
        r = term_set(tty_fd,
                     1,              /* raw mode. */
                     opts.baud,      /* baud rate. */
                     opts.parity,    /* parity. */
                     opts.databits,  /* data bits. */
                     opts.stopbits,  /* stop bits. */
                     opts.flow,      /* flow control. */
                     1,              /* local or modem */
                     !opts.noreset); /* hup-on-close. */
    }
    if ( r < 0 )
        fatal("failed to add port: %s", term_strerror(term_errno, errno));
    /* Set DTR and RTS status, as quickly as possible after opening
       the serial port (i.e. before configuring it) */
    set_dtr_rts();
    r = term_apply(tty_fd, 0);
    if ( r < 0 )
        fatal("failed to config port: %s",
              term_strerror(term_errno, errno));
    /* Set DTR and RTS status *again* after configuring the port. On
       some systems term_apply() resets the status of DTR and / or
       RTS */
    set_dtr_rts();

    set_tty_write_sz(term_get_baudrate(tty_fd, NULL));

    /* Check for settings mismatch and print warning */
    if ( !opts.quiet && !opts.noinit && show_status(1) != 0 ) {
        pinfo("!! Settings mismatch !!");
        if ( ! opts.noescape )
            pinfo(" Type [C-%c] [C-%c] to see actual port settings",
                  KEYC(opts.escape), KEYC(KEY_STATUS));
        pinfo("\r\n");
    }

    if ( ! opts.exit ) {
        if ( isatty(STI) ) {
            r = term_add(STI);
            if ( r < 0 )
                fatal("failed to add I/O device: %s",
                      term_strerror(term_errno, errno));
            term_set_raw(STI);
            r = term_apply(STI, 0);
            if ( r < 0 )
                fatal("failed to set I/O device to raw mode: %s",
                      term_strerror(term_errno, errno));
        } else {
            pinfo("!! STDIN is not a TTY !! Continue anyway...\r\n");
        }
    } else {
        close(STI);
    }

#ifdef LINENOISE
    init_history();
#endif

    /* Allocate output buffer with initial size */
    tty_q.buff = calloc(TTY_Q_SZ_MIN, sizeof(*tty_q.buff));
    if ( ! tty_q.buff )
        fatal("out of memory");
    tty_q.sz = TTY_Q_SZ_MIN;
    tty_q.len = 0;

    /* Prime output buffer with initstring */
    if ( opts.initstring ) {
        if ( opts.noinit ) {
            pinfo("Ignoring init-string (--noinit)\r\n");
        } else {
            int l;
            l = strlen(opts.initstring);
            if ( tty_q_push(opts.initstring, l) != l ) {
                fatal("initstring too long!");
            }
        }
    }
    /* Free initstirng, no longer needed */
    if ( opts.initstring ) {
        free(opts.initstring);
        opts.initstring = NULL;
    }

#ifndef NO_HELP
    if ( ! opts.noescape ) {
        pinfo("Type [C-%c] [C-%c] to see available commands\r\n",
              KEYC(opts.escape), KEYC(KEY_HELP));
    }
#endif
    pinfo("Terminal ready\r\n");

    /* Enter main processing loop */
    ler = loop();

    /* Terminating picocom */
    pinfo("\r\n");
    pinfo("Terminating...\r\n");

    if ( ler == LE_CMD || ler == LE_SIGNAL )
        cleanup(0 /* drain */, opts.noreset, opts.hangup);
    else
        cleanup(1 /* drain */, opts.noreset, opts.hangup);

    if ( ler == LE_SIGNAL ) {
        pinfo("Picocom was killed\r\n");
        xcode = EXIT_FAILURE;
    } else
        pinfo("Thanks for using picocom\r\n");

    return xcode;
}

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
