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
#ifdef USE_FLOCK
#include <sys/file.h>
#endif
#ifdef LINENOISE
#include <dirent.h>
#include <libgen.h>
#endif

#define _GNU_SOURCE
#include <getopt.h>

#include "split.h"
#include "term.h"
#ifdef LINENOISE
#include "linenoise-1.0/linenoise.h"
#endif

/**********************************************************************/

/* parity modes names */
const char *parity_str[] = {
	[P_NONE] = "none",
	[P_EVEN] = "even",
	[P_ODD] = "odd",
	[P_MARK] = "mark",
	[P_SPACE] = "space",
};

/* flow control modes names */
const char *flow_str[] = {
	[FC_NONE] = "none",
	[FC_RTSCTS] = "RTS/CTS",
	[FC_XONXOFF] = "xon/xoff",
	[FC_OTHER] = "other",
};

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
	enum parity_e parity;
	int databits;
	int lecho;
	int noinit;
	int noreset;
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
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
	.parity = P_NONE,
	.databits = 8,
	.lecho = 0,
	.noinit = 0,
	.noreset = 0,
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
	.nolock = 0,
#endif
	.escape = '\x01',
	.send_cmd = "sz -vv",
	.receive_cmd = "rz -vv",
	.imap = M_I_DFL,
	.omap = M_O_DFL,
	.emap = M_E_DFL
};

int sig_exit = 0;

#define STO STDOUT_FILENO
#define STI STDIN_FILENO

int tty_fd;

#ifndef TTY_Q_SZ
#define TTY_Q_SZ 256
#endif

struct tty_q {
	int len;
	unsigned char buff[TTY_Q_SZ];
} tty_q;

int tty_write_sz;

#define TTY_WRITE_SZ_DIV 10
#define TTY_WRITE_SZ_MIN 8

#define set_tty_write_sz(baud)							\
    do {												\
        tty_write_sz = (baud) / TTY_WRITE_SZ_DIV;		\
	    if ( tty_write_sz < TTY_WRITE_SZ_MIN )			\
            tty_write_sz = TTY_WRITE_SZ_MIN;			\
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

/**********************************************************************/

#ifndef LINENOISE

int cput(int fd, char c) { return write(fd, &c, 1); }

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
				cput(fdo, '\b'); cput(fdo, ' '); cput(fdo, '\b');
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
			if ( bp < bpe ) { *bp++ = c; cput(fdo, c); }
			else { cput(fdo, '\x07'); }
			break;
		}
	}

out:
	return r;
}

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

#else /* LINENOISE defined */

void 
file_completion_cb (const char *buf, linenoiseCompletions *lc) 
{
	DIR *dirp;
	struct dirent *dp;
	char *basec, *basen, *dirc, *dirn;
	int baselen, dirlen;
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
			if (strncmp(basen, dp->d_name, baselen) == 0) {
				/* add 2 extra bytes for possible / in middle & at end */
				fullpath = (char *) malloc(strlen(dp->d_name) + dirlen + 3);
				strcpy(fullpath, dirn);
				if (fullpath[dirlen-1] != '/')
					strcat(fullpath, "/");
				strcat(fullpath, dp->d_name);
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

static char *send_receive_history_file_path = NULL;

void 
init_send_receive_history (void)
{
	char *home_directory;

	home_directory = getenv("HOME");
	if (home_directory) {
		send_receive_history_file_path = 
			malloc(strlen(home_directory) + 2 + 
				   strlen(SEND_RECEIVE_HISTFILE));
		strcpy(send_receive_history_file_path, home_directory);
		if (home_directory[strlen(home_directory)-1] != '/') {
			strcat(send_receive_history_file_path, "/");
		}
		strcat(send_receive_history_file_path, SEND_RECEIVE_HISTFILE);
		linenoiseHistoryLoad(send_receive_history_file_path);
	}
}

void 
cleanup_send_receive_history (void)
{
	if (send_receive_history_file_path)
		free(send_receive_history_file_path);
}

void 
add_send_receive_history (char *fname)
{
	linenoiseHistoryAdd(fname);
	if (send_receive_history_file_path)
		linenoiseHistorySave(send_receive_history_file_path);
}

char *
read_filename (void)
{
	char *fname;
	linenoiseSetCompletionCallback(file_completion_cb);
	printf("\r\n");
	fname = linenoise("*** file: ");
	printf("\r\n");
	linenoiseSetCompletionCallback(NULL);
	if (fname != NULL)
		add_send_receive_history(fname);
	return fname;
}

#endif /* of ifndef LINENOISE */

/**********************************************************************/

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

int
flow_next (int flow)
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

int
parity_next (int parity)
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

void
show_status (int dtr_up) 
{
	int baud, bits;
	enum flowcntrl_e flow;
	enum parity_e parity;

	term_refresh(tty_fd);

	baud = term_get_baudrate(tty_fd, NULL);
	flow = term_get_flowcntrl(tty_fd);
	parity = term_get_parity(tty_fd);
	bits = term_get_databits(tty_fd);
	
	fd_printf(STO, "\r\n");
 
	if ( baud != opts.baud ) {
		fd_printf(STO, "*** baud: %d (%d)\r\n", opts.baud, baud);
	} else { 
		fd_printf(STO, "*** baud: %d\r\n", opts.baud);
	}
	if ( flow != opts.flow ) {
		fd_printf(STO, "*** flow: %s (%s)\r\n", 
				  flow_str[opts.flow], flow_str[flow]);
	} else {
		fd_printf(STO, "*** flow: %s\r\n", flow_str[opts.flow]);
	}
	if ( parity != opts.parity ) {
		fd_printf(STO, "*** parity: %s (%s)\r\n", 
				  parity_str[opts.parity], parity_str[parity]);
	} else {
		fd_printf(STO, "*** parity: %s\r\n", parity_str[opts.parity]);
	}
	if ( bits != opts.databits ) {
		fd_printf(STO, "*** databits: %d (%d)\r\n", opts.databits, bits);
	} else {
		fd_printf(STO, "*** databits: %d\r\n", opts.databits);
	}
	fd_printf(STO, "*** dtr: %s\r\n", dtr_up ? "up" : "down");
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

	/* block signals, let child establish its own handlers */
	sigemptyset(&sigm);
	sigaddset(&sigm, SIGTERM);
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
		term_apply(STI);
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
			fd_printf(STDERR_FILENO, "Cannot parse command\n");
			exit(RUNCMD_EXEC_FAIL);
		}
		r = split_quoted(args_extra, &argc, argv, RUNCMD_ARGS_MAX);
		if ( r < 0 ) {
			fd_printf(STDERR_FILENO, "Cannot parse extra args\n");
			exit(RUNCMD_EXEC_FAIL);
		}
		if ( argc < 1 ) {
			fd_printf(STDERR_FILENO, "No command given\n");
			exit(RUNCMD_EXEC_FAIL);
		}	
		argv[argc] = NULL;
			
		/* run extenral command */
		fd_printf(STDERR_FILENO, "$ %s %s\n", cmd, args_extra);
		establish_child_signal_handlers();
		sigprocmask(SIG_SETMASK, &sigm_old, NULL);
		execvp(argv[0], argv);

		fd_printf(STDERR_FILENO, "exec: %s\n", strerror(errno));
		exit(RUNCMD_EXEC_FAIL);
	}
}

/**********************************************************************/

/* Process command key. Returns non-zero if command results in picocom
   exit, zero otherwise. */
int
do_command (unsigned char c)
{
	static int dtr_up = 0;
	int newbaud, newflow, newparity, newbits;
	const char *xfr_cmd;
	char *fname;
	int r;

	switch (c) {
	case KEY_EXIT:
		return 1;
	case KEY_QUIT:
		term_set_hupcl(tty_fd, 0);
		term_flush(tty_fd);
		term_apply(tty_fd);
		term_erase(tty_fd);
		return 1;
	case KEY_STATUS:
		show_status(dtr_up);
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
	case KEY_BAUD_DN:
		if (c == KEY_BAUD_UP)
			opts.baud = baud_up(opts.baud);
		else 
			opts.baud = baud_down(opts.baud);
		term_set_baudrate(tty_fd, opts.baud);
		tty_q.len = 0; term_flush(tty_fd);
		term_apply(tty_fd);
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
		term_apply(tty_fd);
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
		term_apply(tty_fd);
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
		term_apply(tty_fd);
		newbits = term_get_databits(tty_fd);
		if (opts.databits != newbits ) {
			fd_printf(STO, "\r\n*** databits: %d (%d) ***\r\n",
					  opts.databits, newbits);
		} else {
			fd_printf(STO, "\r\n*** databits: %d ***\r\n", 
					  opts.databits);
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

void
loop(void)
{
	enum {
		ST_COMMAND,
		ST_TRANSPARENT
	} state;
	fd_set rdset, wrset;
	int r, n;
	unsigned char c;

	tty_q.len = 0;
	state = ST_TRANSPARENT;

	while ( ! sig_exit ) {
		FD_ZERO(&rdset);
		FD_ZERO(&wrset);
		FD_SET(STI, &rdset);
		FD_SET(tty_fd, &rdset);
		if ( tty_q.len ) FD_SET(tty_fd, &wrset);

		r = select(tty_fd + 1, &rdset, &wrset, NULL, NULL);
		if ( r < 0 )  {
			if ( errno == EINTR )
				continue;
			else
				fatal("select failed: %d : %s", errno, strerror(errno));
		}

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
					/* pass the escape character down */
					if (tty_q.len + M_MAXMAP <= TTY_Q_SZ) {
						n = do_map((char *)tty_q.buff + tty_q.len, 
								   opts.omap, c);
						tty_q.len += n;
						if ( opts.lecho ) 
							map_and_write(STO, opts.emap, c);
					} else {
						fd_printf(STO, "\x07");
					}
				} else {
					/* process command key */
					if ( do_command(c) )
						/* picocom exit */
						return;
				}
				state = ST_TRANSPARENT;
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

			int sz;
			sz = (tty_q.len < tty_write_sz) ? tty_q.len : tty_write_sz;
			do {
				n = write(tty_fd, tty_q.buff, sz);
			} while ( n < 0 && errno == EINTR );
			if ( n <= 0 )
				fatal("write to term failed: %s", strerror(errno));
			memmove(tty_q.buff, tty_q.buff + n, tty_q.len - n);
			tty_q.len -= n;
		}
	}
}

/**********************************************************************/

void
deadly_handler(int signum)
{
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

        sigaction (SIGINT, &ign_action, NULL); 
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
	char *s;

	s = strrchr(name, '/');
	s = s ? s+1 : name;

	printf("picocom v%s\n", VERSION_STR);

	printf("\nCompiled-in options:\n");
	printf("  TTY_Q_SZ is %d\n", TTY_Q_SZ);
#ifdef USE_HIGH_BAUD
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
	printf("  SEND_RECEIVE_HISTFILE is: %s\n", SEND_RECEIVE_HISTFILE);
#endif
	
	printf("\nUsage is: %s [options] <tty device>\n", s);
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
	int r;

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

	r = 0;
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
		case 'l':
#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)
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
			break;
		case 'p':
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

	if ( (argc - optind) < 1) {
		fprintf(stderr, "No port given\n");
		fprintf(stderr, "Run with '--help'.\n");
		exit(EXIT_FAILURE);
	}
	strncpy(opts.port, argv[optind], sizeof(opts.port) - 1);
	opts.port[sizeof(opts.port) - 1] = '\0';

	printf("picocom v%s\n", VERSION_STR);
	printf("\n");
	printf("port is        : %s\n", opts.port);
	printf("flowcontrol    : %s\n", flow_str[opts.flow]);
	printf("baudrate is    : %d\n", opts.baud);
	printf("parity is      : %s\n", parity_str[opts.parity]);
	printf("databits are   : %d\n", opts.databits);
	printf("escape is      : C-%c\n", 'a' + opts.escape - 1);
	printf("local echo is  : %s\n", opts.lecho ? "yes" : "no");
	printf("noinit is      : %s\n", opts.noinit ? "yes" : "no");
	printf("noreset is     : %s\n", opts.noreset ? "yes" : "no");
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

	set_tty_write_sz(term_get_baudrate(tty_fd, NULL));
	
	r = term_add(STI);
	if ( r < 0 )
		fatal("failed to add I/O device: %s", 
			  term_strerror(term_errno, errno));
	term_set_raw(STI);
	r = term_apply(STI);
	if ( r < 0 )
		fatal("failed to set I/O device to raw mode: %s",
			  term_strerror(term_errno, errno));

#ifdef LINENOISE
	init_send_receive_history();
#endif

	fd_printf(STO, "Terminal ready\r\n");
	loop();

#ifdef LINENOISE
	cleanup_send_receive_history();
#endif

	fd_printf(STO, "\r\n");
	if ( opts.noreset ) {
		fd_printf(STO, "Skipping tty reset...\r\n");
		term_erase(tty_fd);
	}

	if ( sig_exit )
		fd_printf(STO, "Picocom was killed\r\n");
	else
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
