/* vi: set sw=4 ts=4:
 *
 * picocom.c
 *
 * simple dumb-terminal program. Helps you manually configure and test
 * stuff like modems, devices w. serial ports etc.
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

#define _GNU_SOURCE
#include <getopt.h>

#include "term.h"

/**********************************************************************/

#define KEY_EXIT    '\x18' /* C-x: exit picocom */
#define KEY_QUIT    '\x11' /* C-q: exit picocom without reseting port */
#define KEY_PULSE   '\x10' /* C-p: pulse DTR */
#define KEY_TOGGLE  '\x14' /* C-t: toggle DTR */
#define KEY_BAUD_UP '\x15' /* C-u: increase baudrate (up) */
#define KEY_BAUD_DN '\x04' /* C-d: decrase baudrate (down) */ 
#define KEY_FLOW    '\x06' /* C-f: change flowcntrl mode */ 
#define KEY_PARITY  '\x19' /* C-y: change parity mode */ 
#define KEY_BITS    '\x02' /* C-b: change number of databits */ 
#define KEY_LECHO   '\x03' /* C-c: toggle local echo */ 
#define KEY_STATUS  '\x16' /* C-v: show program option */
#define KEY_SEND    '\x13' /* C-s: send file */
#define KEY_RECEIVE '\x12' /* C-r: receive file */
#define KEY_BREAK   '\x1c' /* C-\: break */

#define STO STDOUT_FILENO
#define STI STDIN_FILENO

/**********************************************************************/

/* implemented caracter mappings */
#define M_CRLF   (1 << 0) /* map CR  --> LF */
#define M_CRCRLF (1 << 1) /* map CR  --> CR + LF */
#define M_IGNCR  (1 << 2) /* map CR  --> <nothing> */
#define M_LFCR   (1 << 3) /* map LF  --> CR */
#define M_LFCRLF (1 << 4) /* map LF  --> CR + LF */
#define M_IGNLF  (1 << 5) /* map LF  --> <nothing> */
#define M_DELBS  (1 << 6) /* map DEL --> BS */
#define M_BSDEL  (1 << 7) /* map BS  --> DEL */
#define M_NFLAGS 8

/* default character mappings */
#define M_I_DFL 0
#define M_O_DFL 0
#define M_E_DFL (M_DELBS | M_CRCRLF)

/* character mapping names */
struct map_names_s {
	char *name;
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
	/* Sentinel */
	{ NULL, 0 } 
};

int
parse_map (char *s)
{
	char *m, *t;
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
	char port[128];
	int baud;
	enum flowcntrl_e flow;
	char *flow_str;
	enum parity_e parity;
	char *parity_str;
	int databits;
	int lecho;
	int noinit;
	int noreset;
#ifdef UUCP_LOCK_DIR
	int nolock;
#endif
	unsigned char escape;
	char send_cmd[128];
	char receive_cmd[128];
	int imap;
	int omap;
	int emap;
} opts = {
	.port = "",
	.baud = 9600,
	.flow = FC_NONE,
	.flow_str = "none",
	.parity = P_NONE,
	.parity_str = "none",
	.databits = 8,
	.lecho = 0,
	.noinit = 0,
	.noreset = 0,
#ifdef UUCP_LOCK_DIR
	.nolock = 0,
#endif
	.escape = '\x01',
	.send_cmd = "sz -vv",
	.receive_cmd = "rz -vv",
	.imap = M_I_DFL,
	.omap = M_O_DFL,
	.emap = M_E_DFL
};

int tty_fd;

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
			printf("Removing stale lock: %s\n", lockname);
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

ssize_t
writen_ni(int fd, const void *buff, size_t n)
{
	size_t nl; 
	ssize_t nw;
	const char *p;

	p = buff;
	nl = n;
	while (nl > 0) {
		do {
			nw = write(fd, p, nl);
		} while ( nw < 0 && errno == EINTR );
		if ( nw <= 0 ) break;
		nl -= nw;
		p += nw;
	}
	
	return n - nl;
}

int
fd_printf (int fd, const char *format, ...)
{
	char buf[256];
	va_list args;
	int len;
	
	va_start(args, format);
	len = vsnprintf(buf, sizeof(buf), format, args);
	buf[sizeof(buf) - 1] = '\0';
	va_end(args);
	
	return writen_ni(fd, buf, len);
}

void
fatal (const char *format, ...)
{
	char *s, buf[256];
	va_list args;
	int len;

	term_reset(STO);
	term_reset(STI);
	
	va_start(args, format);
	len = vsnprintf(buf, sizeof(buf), format, args);
	buf[sizeof(buf) - 1] = '\0';
	va_end(args);
	
	s = "\r\nFATAL: ";
	writen_ni(STO, s, strlen(s));
	writen_ni(STO, buf, len);
	s = "\r\n";
	writen_ni(STO, s, strlen(s));

	/* wait a bit for output to drain */
	sleep(1);

#ifdef UUCP_LOCK_DIR
	uucp_unlock();
#endif
	
	exit(EXIT_FAILURE);
}

#define cput(fd, c) do { int cl = c; write((fd), &(cl), 1); } while(0)

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
		if ( r <= 0 ) { r--; goto out; }

		switch (c) {
		case '\b':
			if ( bp > (unsigned char *)b ) { 
				bp--;
				cput(fdo, c); cput(fdo, ' '); cput(fdo, c);
			} else {
				cput(fdo, '\x07');
			}
			break;
		case '\r':
			*bp = '\0';
			r = bp - (unsigned char *)b; 
			goto out;
		default:
			if ( bp < bpe ) { *bp++ = c; cput(fdo, c); }
			else { cput(fdo, '\x07'); }
			break;
		}
	}

out:
	return r;
}

#undef cput

/* maximum number of chars that can replace a single characted
   due to mapping */
#define M_MAXMAP 4

int
do_map (char *b, int map, char c)
{
	int n;

	switch (c) {
	case '\x7f':
		/* DEL mapings */
		if ( map & M_DELBS ) {
			b[0] = '\x08'; n = 1;
		} else {
			b[0] = c; n = 1;
		}
		break;
	case '\x08':
		/* BS mapings */
		if ( map & M_BSDEL ) {
			b[0] = '\x7f'; n = 1;
		} else {
			b[0] = c; n = 1;
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
		} else {
			b[0] = c; n = 1;
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
		} else {
			b[0] = c; n = 1;
		}
		break;
	default:
		b[0] = c; n = 1;
		break;
	}

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
	if ( baud < 300 )
		baud = 300;
	else if ( baud == 38400 )
		baud = 57600;
	else	
		baud = baud * 2;
#ifndef HIGH_BAUD
	if ( baud > 115200 )
		baud = 115200;
#else
	if ( baud > 921600 )
		baud = 921600;
#endif

	return baud;
}

int
baud_down (int baud)
{
#ifndef HIGH_BAUD
	if ( baud > 115200 )
		baud = 115200;
#else
	if ( baud > 921600 )
		baud = 921600;
#endif
	else if ( baud == 57600 )
		baud = 38400;
	else
		baud = baud / 2;

	if ( baud < 300)
		baud = 300;

	return baud;
}

int
flow_next (int flow, char **flow_str)
{
	switch(flow) {
	case FC_NONE:
		flow = FC_RTSCTS;
		*flow_str = "RTS/CTS";
		break;
	case FC_RTSCTS:
		flow = FC_XONXOFF;
		*flow_str = "xon/xoff";
		break;
	case FC_XONXOFF:
		flow = FC_NONE;
		*flow_str = "none";
		break;
	default:
		flow = FC_NONE;
		*flow_str = "none";
		break;
	}

	return flow;
}

int
parity_next (int parity, char **parity_str)
{
	switch(parity) {
	case P_NONE:
		parity = P_EVEN;
		*parity_str = "even";
		break;
	case P_EVEN:
		parity = P_ODD;
		*parity_str = "odd";
		break;
	case P_ODD:
		parity = P_NONE;
		*parity_str = "none";
		break;
	default:
		parity = P_NONE;
		*parity_str = "none";
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

/**********************************************************************/

void
child_empty_handler (int signum)
{
}

void
establish_child_signal_handlers (void)
{
	struct sigaction empty_action;
	
	/* Set up the structure to specify the "empty" action. */
    empty_action.sa_handler = child_empty_handler;
	sigemptyset (&empty_action.sa_mask);
	empty_action.sa_flags = 0;
	
	sigaction (SIGINT, &empty_action, NULL);
	sigaction (SIGTERM, &empty_action, NULL);
}

int
run_cmd(int fd, ...)
{
	pid_t pid;
	sigset_t sigm, sigm_old;

	/* block signals, let child establish its own handlers */
	sigemptyset(&sigm);
	sigaddset(&sigm, SIGTERM);
	sigprocmask(SIG_BLOCK, &sigm, &sigm_old);

	pid = fork();
	if ( pid < 0 ) {
		sigprocmask(SIG_SETMASK, &sigm_old, NULL);
		fd_printf(STO, "*** cannot fork: %s\n", strerror(errno));
		return -1;
	} else if ( pid ) {
		/* father: picocom */
		int r;

		/* reset the mask */
		sigprocmask(SIG_SETMASK, &sigm_old, NULL);
		/* wait for child to finish */
		waitpid(pid, &r, 0);
		/* reset terminal (back to raw mode) */
		term_apply(STI);
		/* check and report child return status */
		if ( WIFEXITED(r) ) { 
			fd_printf(STO, "\r\n*** exit status: %d\r\n", 
					  WEXITSTATUS(r));
			return WEXITSTATUS(r);
		} else {
			fd_printf(STO, "\r\n*** abnormal termination: 0x%x\r\n", r);
			return -1;
		}
	} else {
		/* child: external program */
		int r;
		long fl;
		char cmd[512];

		establish_child_signal_handlers();
		sigprocmask(SIG_SETMASK, &sigm_old, NULL);
		/* unmanage terminal, and reset it to canonical mode */
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
		{
			/* build command-line */
			char *c, *ce;
			const char *s;
			int n;
			va_list vls;
			
			c = cmd;
			ce = cmd + sizeof(cmd) - 1;
			va_start(vls, fd);
			while ( (s = va_arg(vls, const char *)) ) {
				n = strlen(s);
				if ( c + n + 1 >= ce ) break;
				memcpy(c, s, n); c += n;
				*c++ = ' ';
			}
			va_end(vls);
			*c = '\0';
		}
		/* run extenral command */
		fd_printf(STDERR_FILENO, "%s\n", cmd);
		r = system(cmd);
		if ( WIFEXITED(r) ) exit(WEXITSTATUS(r));
		else exit(128);
	}
}

/**********************************************************************/

#define TTY_Q_SZ 256

struct tty_q {
	int len;
	unsigned char buff[TTY_Q_SZ];
} tty_q;

/**********************************************************************/

void
loop(void)
{
	enum {
		ST_COMMAND,
		ST_TRANSPARENT
	} state;
	int dtr_up;
	fd_set rdset, wrset;
	int newbaud, newflow, newparity, newbits;
	char *newflow_str, *newparity_str;
	char fname[128];
	int r, n;
	unsigned char c;


	tty_q.len = 0;
	state = ST_TRANSPARENT;
	dtr_up = 0;

	for (;;) {
		FD_ZERO(&rdset);
		FD_ZERO(&wrset);
		FD_SET(STI, &rdset);
		FD_SET(tty_fd, &rdset);
		if ( tty_q.len ) FD_SET(tty_fd, &wrset);

		if (select(tty_fd + 1, &rdset, &wrset, NULL, NULL) < 0)
			fatal("select failed: %d : %s", errno, strerror(errno));

		if ( FD_ISSET(STI, &rdset) ) {

			/* read from terminal */

			do {
				n = read(STI, &c, 1);
			} while (n < 0 && errno == EINTR);
			if (n == 0) {
				fatal("stdin closed");
			} else if (n < 0) {
				/* is this really necessary? better safe than sory! */
				if ( errno != EAGAIN && errno != EWOULDBLOCK ) 
					fatal("read from stdin failed: %s", strerror(errno));
				else
					goto skip_proc_STI;
			}

			switch (state) {

			case ST_COMMAND:
				if ( c == opts.escape ) {
					state = ST_TRANSPARENT;
					/* pass the escape character down */
					if (tty_q.len + M_MAXMAP <= TTY_Q_SZ) {
						n = do_map((char *)tty_q.buff + tty_q.len, 
								   opts.omap, c);
						tty_q.len += n;
						if ( opts.lecho ) 
							map_and_write(STO, opts.emap, c);
					} else 
						fd_printf(STO, "\x07");
					break;
				}
				state = ST_TRANSPARENT;
				switch (c) {
				case KEY_EXIT:
					return;
				case KEY_QUIT:
					term_set_hupcl(tty_fd, 0);
					term_flush(tty_fd);
					term_apply(tty_fd);
					term_erase(tty_fd);
					return;
				case KEY_STATUS:
					fd_printf(STO, "\r\n");
					fd_printf(STO, "*** baud: %d\r\n", opts.baud);
					fd_printf(STO, "*** flow: %s\r\n", opts.flow_str);
					fd_printf(STO, "*** parity: %s\r\n", opts.parity_str);
					fd_printf(STO, "*** databits: %d\r\n", opts.databits);
					fd_printf(STO, "*** dtr: %s\r\n", dtr_up ? "up" : "down");
					break;
				case KEY_PULSE:
					fd_printf(STO, "\r\n*** pulse DTR ***\r\n");
					if ( term_pulse_dtr(tty_fd) < 0 )
						fd_printf(STO, "*** FAILED\r\n");
					break;
				case KEY_TOGGLE:
					if ( dtr_up )
						r = term_lower_dtr(tty_fd);
					else
						r = term_raise_dtr(tty_fd);
					if ( r >= 0 ) dtr_up = ! dtr_up;
					fd_printf(STO, "\r\n*** DTR: %s ***\r\n", 
							  dtr_up ? "up" : "down");
					break;
				case KEY_BAUD_UP:
					newbaud = baud_up(opts.baud);
					term_set_baudrate(tty_fd, newbaud);
					tty_q.len = 0; term_flush(tty_fd);
					if ( term_apply(tty_fd) >= 0 ) opts.baud = newbaud;
					fd_printf(STO, "\r\n*** baud: %d ***\r\n", opts.baud);
					break;
				case KEY_BAUD_DN:
					newbaud = baud_down(opts.baud);
					term_set_baudrate(tty_fd, newbaud);
					tty_q.len = 0; term_flush(tty_fd);
					if ( term_apply(tty_fd) >= 0 ) opts.baud = newbaud;
					fd_printf(STO, "\r\n*** baud: %d ***\r\n", opts.baud);
					break;
				case KEY_FLOW:
					newflow = flow_next(opts.flow, &newflow_str);
					term_set_flowcntrl(tty_fd, newflow);
					tty_q.len = 0; term_flush(tty_fd);
					if ( term_apply(tty_fd) >= 0 ) {
						opts.flow = newflow;
						opts.flow_str = newflow_str;
					}
					fd_printf(STO, "\r\n*** flow: %s ***\r\n", opts.flow_str);
					break;
				case KEY_PARITY:
					newparity = parity_next(opts.parity, &newparity_str);
					term_set_parity(tty_fd, newparity);
					tty_q.len = 0; term_flush(tty_fd);
					if ( term_apply(tty_fd) >= 0 ) {
						opts.parity = newparity;
						opts.parity_str = newparity_str;
					}
					fd_printf(STO, "\r\n*** parity: %s ***\r\n", 
							  opts.parity_str);
					break;
				case KEY_BITS:
					newbits = bits_next(opts.databits);
					term_set_databits(tty_fd, newbits);
					tty_q.len = 0; term_flush(tty_fd);
					if ( term_apply(tty_fd) >= 0 ) opts.databits = newbits;
					fd_printf(STO, "\r\n*** databits: %d ***\r\n", 
							  opts.databits);
					break;
				case KEY_LECHO:
					opts.lecho = ! opts.lecho;
					fd_printf(STO, "\r\n*** local echo: %s ***\r\n", 
							  opts.lecho ? "yes" : "no");
					break;
				case KEY_SEND:
					fd_printf(STO, "\r\n*** file: ");
					r = fd_readline(STI, STO, fname, sizeof(fname));
					fd_printf(STO, "\r\n");
					if ( r < -1 && errno == EINTR ) break;
					if ( r <= -1 )
						fatal("cannot read filename: %s", strerror(errno));
					run_cmd(tty_fd, opts.send_cmd, fname, NULL);
					break;
				case KEY_RECEIVE:
					fd_printf(STO, "*** file: ");
					r = fd_readline(STI, STO, fname, sizeof(fname));
					fd_printf(STO, "\r\n");
					if ( r < -1 && errno == EINTR ) break;
					if ( r <= -1 )
						fatal("cannot read filename: %s", strerror(errno));
					if ( fname[0] )
						run_cmd(tty_fd, opts.receive_cmd, fname, NULL);
					else
						run_cmd(tty_fd, opts.receive_cmd, NULL);
					break;
				case KEY_BREAK:
					term_break(tty_fd);
					fd_printf(STO, "\r\n*** break sent ***\r\n");
					break;
				default:
					break;
				}
				break;

			case ST_TRANSPARENT:
				if ( c == opts.escape ) {
					state = ST_COMMAND;
				} else {
					if (tty_q.len + M_MAXMAP <= TTY_Q_SZ) {
						n = do_map((char *)tty_q.buff + tty_q.len, 
								   opts.omap, c);
						tty_q.len += n;
						if ( opts.lecho ) 
							map_and_write(STO, opts.emap, c);
					} else 
						fd_printf(STO, "\x07");
				}
				break;

			default:
				assert(0);
				break;
			}
		}
	skip_proc_STI:

		if ( FD_ISSET(tty_fd, &rdset) ) {

			/* read from port */

			do {
				n = read(tty_fd, &c, 1);
			} while (n < 0 && errno == EINTR);
			if (n == 0) {
				fatal("term closed");
			} else if ( n < 0 ) {
				if ( errno != EAGAIN && errno != EWOULDBLOCK )
					fatal("read from term failed: %s", strerror(errno));
			} else {
				map_and_write(STO, opts.imap, c);
			}
		}

		if ( FD_ISSET(tty_fd, &wrset) ) {

			/* write to port */

			do {
				n = write(tty_fd, tty_q.buff, tty_q.len);
			} while ( n < 0 && errno == EINTR );
			if ( n <= 0 )
				fatal("write to term failed: %s", strerror(errno));
			memcpy(tty_q.buff, tty_q.buff + n, tty_q.len - n);
			tty_q.len -= n;
		}
	}
}

/**********************************************************************/

void
deadly_handler(int signum)
{
	kill(0, SIGTERM);
	sleep(1);
#ifdef UUCP_LOCK_DIR
	uucp_unlock();
#endif
	exit(EXIT_FAILURE);
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

        sigaction (SIGINT, &ign_action, NULL); 
        sigaction (SIGHUP, &ign_action, NULL);
        sigaction (SIGALRM, &ign_action, NULL);
        sigaction (SIGUSR1, &ign_action, NULL);
        sigaction (SIGUSR2, &ign_action, NULL);
        sigaction (SIGPIPE, &ign_action, NULL);
}

/**********************************************************************/

void
show_usage(char *name)
{
	char *s;

	s = strrchr(name, '/');
	s = s ? s+1 : name;

	printf("picocom v%s\n", VERSION_STR);
	printf("Usage is: %s [options] <tty device>\n", s);
	printf("Options are:\n");
	printf("  --<b>aud <baudrate>\n");
	printf("  --<f>low s (=soft) | h (=hard) | n (=none)\n");
	printf("  --<p>arity o (=odd) | e (=even) | n (=none)\n");
	printf("  --<d>atabits 5 | 6 | 7 | 8\n");
	printf("  --<e>scape <char>\n");
	printf("  --e<c>ho\n");
	printf("  --no<i>nit\n");
	printf("  --no<r>eset\n");
	printf("  --no<l>ock\n");
	printf("  --<s>end-cmd <command>\n");
	printf("  --recei<v>e-cmd <command>\n");
	printf("  --imap <map> (input mappings)\n");
	printf("  --omap <map> (output mappings)\n");
	printf("  --emap <map> (local-echo mappings)\n");
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
	printf("<?> indicates the equivalent short option.\n");
	printf("Short options are prefixed by \"-\" instead of by \"--\".\n");
}

/**********************************************************************/

void
parse_args(int argc, char *argv[])
{
	static struct option longOptions[] =
	{
		{"receive-cmd", required_argument, 0, 'v'},
		{"send-cmd", required_argument, 0, 's'},
        {"imap", required_argument, 0, 'I' },
        {"omap", required_argument, 0, 'O' },
        {"emap", required_argument, 0, 'E' },
		{"escape", required_argument, 0, 'e'},
		{"echo", no_argument, 0, 'c'},
		{"noinit", no_argument, 0, 'i'},
		{"noreset", no_argument, 0, 'r'},
		{"nolock", no_argument, 0, 'l'},
		{"flow", required_argument, 0, 'f'},
		{"baud", required_argument, 0, 'b'},
		{"parity", required_argument, 0, 'p'},
		{"databits", required_argument, 0, 'd'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while (1) {
		int optionIndex = 0;
		int c;
		int map;

		/* no default error messages printed. */
		opterr = 0;

		c = getopt_long(argc, argv, "hirlcv:s:r:e:f:b:p:d:",
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
			else fprintf(stderr, "Invalid imap, ignored\n");
			break;
		case 'O':
			map = parse_map(optarg);
			if (map >= 0) opts.omap = map;
			else fprintf(stderr, "Invalid omap, ignored\n");
			break;
		case 'E':
			map = parse_map(optarg);
			if (map >= 0) opts.emap = map;
			else fprintf(stderr, "Invalid emap, ignored\n");
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
		case 'l':
#ifdef UUCP_LOCK_DIR
			opts.nolock = 1;
#endif
			break;
		case 'e':
			opts.escape = optarg[0] & 0x1f;
			break;
		case 'f':
			switch (optarg[0]) {
			case 'X':
			case 'x':
				opts.flow_str = "xon/xoff";
				opts.flow = FC_XONXOFF;
				break;
			case 'H':
			case 'h':
				opts.flow_str = "RTS/CTS";
				opts.flow = FC_RTSCTS;
				break;
			case 'N':
			case 'n':
				opts.flow_str = "none";
				opts.flow = FC_NONE;
				break;
			default:
				fprintf(stderr, "--flow '%c' ignored.\n", optarg[0]);
				fprintf(stderr, "--flow can be one off: 'x', 'h', or 'n'\n");
				break;
			}
			break;
		case 'b':
			opts.baud = atoi(optarg);
			break;
		case 'p':
			switch (optarg[0]) {
			case 'e':
				opts.parity_str = "even";
				opts.parity = P_EVEN;
				break;
			case 'o':
				opts.parity_str = "odd";
				opts.parity = P_ODD;
				break;
			case 'n':
				opts.parity_str = "none";
				opts.parity = P_NONE;
				break;
			default:
				fprintf(stderr, "--parity '%c' ignored.\n", optarg[0]);
				fprintf(stderr, "--parity can be one off: 'o', 'e', or 'n'\n");
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
				fprintf(stderr, "--databits '%c' ignored.\n", optarg[0]);
				fprintf(stderr, "--databits can be one off: 5, 6, 7 or 8\n");
				break;
			}
			break;
		case 'h':
			show_usage(argv[0]);
			exit(EXIT_SUCCESS);
		case '?':
		default:
			fprintf(stderr, "Unrecognized option.\n");
			fprintf(stderr, "Run with '--help'.\n");
			exit(EXIT_FAILURE);
		}
	} /* while */

	if ( (argc - optind) < 1) {
		fprintf(stderr, "No port given\n");
		exit(EXIT_FAILURE);
	}
	strncpy(opts.port, argv[optind], sizeof(opts.port) - 1);
	opts.port[sizeof(opts.port) - 1] = '\0';

	printf("picocom v%s\n", VERSION_STR);
	printf("\n");
	printf("port is        : %s\n", opts.port);
	printf("flowcontrol    : %s\n", opts.flow_str);
	printf("baudrate is    : %d\n", opts.baud);
	printf("parity is      : %s\n", opts.parity_str);
	printf("databits are   : %d\n", opts.databits);
	printf("escape is      : C-%c\n", 'a' + opts.escape - 1);
	printf("local echo is  : %s\n", opts.lecho ? "yes" : "no");
	printf("noinit is      : %s\n", opts.noinit ? "yes" : "no");
	printf("noreset is     : %s\n", opts.noreset ? "yes" : "no");
#ifdef UUCP_LOCK_DIR
	printf("nolock is      : %s\n", opts.nolock ? "yes" : "no");
#endif
	printf("send_cmd is    : %s\n", opts.send_cmd);
	printf("receive_cmd is : %s\n", opts.receive_cmd);
	printf("imap is        : "); print_map(opts.imap);
	printf("omap is        : "); print_map(opts.omap);
	printf("emap is        : "); print_map(opts.emap);
	printf("\n");
}

/**********************************************************************/


int
main(int argc, char *argv[])
{
	int r;

	parse_args(argc, argv);

	establish_signal_handlers();

	r = term_lib_init();
	if ( r < 0 )
		fatal("term_init failed: %s", term_strerror(term_errno, errno));

#ifdef UUCP_LOCK_DIR
	if ( ! opts.nolock ) uucp_lockname(UUCP_LOCK_DIR, opts.port);
	if ( uucp_lock() < 0 )
		fatal("cannot lock %s: %s", opts.port, strerror(errno));
#endif

	tty_fd = open(opts.port, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (tty_fd < 0)
		fatal("cannot open %s: %s", opts.port, strerror(errno));

	if ( opts.noinit ) {
		r = term_add(tty_fd);
	} else {
		r = term_set(tty_fd,
					 1,              /* raw mode. */
					 opts.baud,      /* baud rate. */
					 opts.parity,    /* parity. */
					 opts.databits,  /* data bits. */
					 opts.flow,      /* flow control. */
					 1,              /* local or modem */
					 !opts.noreset); /* hup-on-close. */
	}
	if ( r < 0 )
		fatal("failed to add device %s: %s", 
			  opts.port, term_strerror(term_errno, errno));
	r = term_apply(tty_fd);
	if ( r < 0 )
		fatal("failed to config device %s: %s", 
			  opts.port, term_strerror(term_errno, errno));
	
	r = term_add(STI);
	if ( r < 0 )
		fatal("failed to add I/O device: %s", 
			  term_strerror(term_errno, errno));
	term_set_raw(STI);
	r = term_apply(STI);
	if ( r < 0 )
		fatal("failed to set I/O device to raw mode: %s",
			  term_strerror(term_errno, errno));

	fd_printf(STO, "Terminal ready\r\n");
	loop();

	fd_printf(STO, "\r\n");
	if ( opts.noreset ) {
		fd_printf(STO, "Skipping tty reset...\r\n");
		term_erase(tty_fd);
	}

	fd_printf(STO, "Thanks for using picocom\r\n");
	/* wait a bit for output to drain */
	sleep(1);

#ifdef UUCP_LOCK_DIR
	uucp_unlock();
#endif

	return EXIT_SUCCESS;
}

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
