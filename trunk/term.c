/* vi: set sw=4 ts=4:
 *
 * term.c
 *
 * General purpose terminal handling library.
 *
 * Nick Patavalis (npat@inaccessnetworks.com)
 *
 * originaly by Pantelis Antoniou (panto@intranet.gr), Nick Patavalis
 *    
 * Documentation can be found in the header file "term.h".
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
 *
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef __linux__
#include <termio.h>
#else
#include <termios.h>
#endif /* of __linux__ */

#include "term.h"

/***************************************************************************/

static struct term_s {
	int init;
	int fd[MAX_TERMS];
	struct termios origtermios[MAX_TERMS];
	struct termios currtermios[MAX_TERMS];
	struct termios nexttermios[MAX_TERMS];
} term;

/***************************************************************************/

int term_errno;

static const char * const term_err_str[] = {
	[TERM_EOK]        = "No error",
	[TERM_ENOINIT]    = "Framework is uninitialized",
	[TERM_EFULL]      = "Framework is full",
    [TERM_ENOTFOUND]  = "Filedes not in the framework",
    [TERM_EEXISTS]    = "Filedes already in the framework",
    [TERM_EATEXIT]    = "Cannot install atexit handler",
    [TERM_EISATTY]    = "Filedes is not a tty",
    [TERM_EFLUSH]     = "Cannot flush the device",
	[TERM_EGETATTR]   = "Cannot get the device attributes",
	[TERM_ESETATTR]   = "Cannot set the device attributes",
	[TERM_EBAUD]      = "Invalid baud rate",
	[TERM_ESETOSPEED] = "Cannot set the output speed",
	[TERM_ESETISPEED] = "Cannot set the input speed",
	[TERM_EPARITY]    = "Invalid parity mode",
	[TERM_EDATABITS]  = "Invalid number of databits",
	[TERM_EFLOW]      = "Invalid flowcontrol mode",
    [TERM_EDTRDOWN]   = "Cannot lower DTR",
    [TERM_EDTRUP]     = "Cannot raise DTR",
	[TERM_EDRAIN]     = "Cannot drain the device",
	[TERM_EBREAK]     = "Cannot send break sequence"
};

static char term_err_buff[1024];

const char *
term_strerror (int terrnum, int errnum)
{
	const char *rval;

	switch(terrnum) {
	case TERM_EFLUSH:
	case TERM_EGETATTR:
	case TERM_ESETATTR:
	case TERM_ESETOSPEED:
	case TERM_ESETISPEED:
	case TERM_EDRAIN:
	case TERM_EBREAK:
		snprintf(term_err_buff, sizeof(term_err_buff),
				 "%s: %s", term_err_str[terrnum], strerror(errnum));
		rval = term_err_buff;
		break;
	case TERM_EOK:
	case TERM_ENOINIT:
	case TERM_EFULL:
	case TERM_ENOTFOUND:
	case TERM_EEXISTS:
	case TERM_EATEXIT:
	case TERM_EISATTY:
	case TERM_EBAUD:
	case TERM_EPARITY:
	case TERM_EDATABITS:
	case TERM_EFLOW:
	case TERM_EDTRDOWN:
	case TERM_EDTRUP:
		snprintf(term_err_buff, sizeof(term_err_buff),
				 "%s", term_err_str[terrnum]);
		rval = term_err_buff;
		break;
	default:
		rval = NULL;
		break;
	}

	return rval;
}

int
term_perror (const char *prefix)
{
	return fprintf(stderr, "%s %s\n",
				   prefix, term_strerror(term_errno, errno));
}

/***************************************************************************/

static int
term_find_next_free (void)
{
	int rval, i;

	do { /* dummy */
		if ( ! term.init ) {
			term_errno = TERM_ENOINIT;
			rval = -1;
			break;
		}

		for (i = 0; i < MAX_TERMS; i++)
			if ( term.fd[i] == -1 ) break;

		if ( i == MAX_TERMS ) {
			term_errno = TERM_EFULL;
			rval = -1;
			break;
		}

		rval = i;
	} while (0);

	return rval;
}

/***************************************************************************/

static int
term_find (int fd)
{
	int rval, i;

	do { /* dummy */
		if ( ! term.init ) {
			term_errno = TERM_ENOINIT;
			rval = -1;
			break;
		}

		for (i = 0; i < MAX_TERMS; i++)
			if (term.fd[i] == fd) break;

		if ( i == MAX_TERMS ) {
			term_errno = TERM_ENOTFOUND;
			rval = -1;
			break;
		}

		rval = i;
	} while (0);

	return rval;
}

/***************************************************************************/

static void
term_exitfunc (void)
{
	int r, i;

	do { /* dummy */
		if ( ! term.init )
			break;

		for (i = 0; i < MAX_TERMS; i++) {
			if (term.fd[i] == -1)
				continue;
			do { /* dummy */
				r = tcflush(term.fd[i], TCIOFLUSH);
				if ( r < 0 ) break;
				r = tcsetattr(term.fd[i], TCSAFLUSH, &term.origtermios[i]);
				if ( r < 0 ) break;
			} while (0);
			if ( r < 0 ) {
				char *tname;

				tname = ttyname(term.fd[i]);
				if ( ! tname ) tname = "UNKNOWN";
				fprintf(stderr, "%s: reset failed for dev %s: %s\n",
						__FUNCTION__, tname, strerror(errno));
			}
			term.fd[i] = -1;
		}
	} while (0);
}

/***************************************************************************/

int
term_lib_init (void)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */
		if ( term.init ) {
			/* reset all terms back to their original settings */
			for (i = 0; i < MAX_TERMS; i++) {
				if (term.fd[i] == -1)
					continue;
				do {
					r = tcflush(term.fd[i], TCIOFLUSH);
					if ( r < 0 ) break;
					r = tcsetattr(term.fd[i], TCSAFLUSH, &term.origtermios[i]);
					if ( r < 0 ) break;
				} while (0);
				if ( r < 0 ) {
					char *tname;
 
					tname = ttyname(term.fd[i]);
					if ( ! tname ) tname = "UNKNOWN";
					fprintf(stderr, "%s: reset failed for dev %s: %s\n",
							__FUNCTION__, tname, strerror(errno));
				}
				term.fd[i] = -1;
			}
		} else {
			/* initialize term structure. */
			for (i = 0; i < MAX_TERMS; i++)
				term.fd[i] = -1;
			if ( atexit(term_exitfunc) != 0 ) {
				term_errno = TERM_EATEXIT;
				rval = -1; 
				break;
			}
			/* ok. term struct is now initialized. */
			term.init = 1;
		}
	} while(0);

	return rval;
}

/***************************************************************************/

int
term_add (int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */
		i = term_find(fd);
		if ( i >= 0 ) {
			term_errno = TERM_EEXISTS;
			rval = -1;
			break;
		}

		if ( ! isatty(fd) ) {
			term_errno = TERM_EISATTY;
			rval = -1;
			break;
		}

		i = term_find_next_free();
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		r = tcgetattr(fd, &term.origtermios[i]);
		if ( r < 0 ) {
			term_errno = TERM_EGETATTR;
			rval = -1;
			break;
		}

		term.currtermios[i] = term.origtermios[i];
		term.nexttermios[i] = term.origtermios[i];
		term.fd[i] = fd;
	} while (0);

	return rval;
}

/***************************************************************************/

int
term_remove(int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */
		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}
		
		do { /* dummy */
			r = tcflush(term.fd[i], TCIOFLUSH);
			if ( r < 0 ) { 
				term_errno = TERM_EFLUSH;
				rval = -1;
				break;
			}
			r = tcsetattr(term.fd[i], TCSAFLUSH, &term.origtermios[i]);
			if ( r < 0 ) {
				term_errno = TERM_ESETATTR;
				rval = -1;
				break;
			}
		} while (0);
		
		term.fd[i] = -1;
	} while (0);

	return rval;
}

/***************************************************************************/

int
term_erase(int fd)
{
	int rval, i;

	rval = 0;

	do { /* dummy */
		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}
		
		term.fd[i] = -1;
	} while (0);

	return rval;
}

/***************************************************************************/

int
term_replace (int oldfd, int newfd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(oldfd); 
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		r = tcsetattr(newfd, TCSAFLUSH, &term.currtermios[i]);
		if ( r < 0 ) {
			term_errno = TERM_ESETATTR;
			rval = -1;
			break;
		}

		term.fd[i] = newfd;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_reset (int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		r = tcflush(term.fd[i], TCIOFLUSH);
		if ( r < 0 ) {
			term_errno = TERM_EFLUSH;
			rval = -1;
			break;
		}
		r = tcsetattr(term.fd[i], TCSAFLUSH, &term.origtermios[i]);
		if ( r < 0 ) {
			term_errno = TERM_ESETATTR;
			rval = -1;
			break;
		}

		term.currtermios[i] = term.origtermios[i];
		term.nexttermios[i] = term.origtermios[i];
	} while (0);

	return rval;
}

/***************************************************************************/

int
term_revert (int fd)
{
	int rval, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		term.nexttermios[i] = term.currtermios[i];

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_refresh (int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		r = tcgetattr(fd, &term.currtermios[i]);
		if ( r < 0 ) {
			term_errno = TERM_EGETATTR;
			rval = -1;
			break;
		}

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_apply (int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}
		
		r = tcsetattr(term.fd[i], TCSAFLUSH, &term.nexttermios[i]);
		if ( r < 0 ) {
			term_errno = TERM_ESETATTR;
			rval = -1;
			break;
		}
		
		term.currtermios[i] = term.nexttermios[i];

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_raw (int fd)
{
	int rval, i;

	rval = 0;

	do { /* dummy */
		
		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		/* BSD raw mode */
		cfmakeraw(&term.nexttermios[i]);
		/* one byte at a time, no timer */
		term.nexttermios[i].c_cc[VMIN] = 1;
		term.nexttermios[i].c_cc[VTIME] = 0;

	} while (0);
	
	return rval;
}

/***************************************************************************/

int
term_set_baudrate (int fd, int baudrate)
{
	int rval, r, i;
	speed_t spd;
	struct termios tio;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		tio = term.nexttermios[i];

		switch (baudrate) {
		case 0:
			spd = B0;
			break;
		case 50:
			spd = B50;
			break;
		case 75:
			spd = B75;
			break;
		case 110:
			spd = B110;
			break;
		case 134:
			spd = B134;
			break;
		case 150:
			spd = B150;
			break;
		case 200:
			spd = B200;
			break;
		case 300:
			spd = B300;
			break;
		case 600:
			spd = B600;
			break;
		case 1200:
			spd = B1200;
			break;
		case 1800:
			spd = B1800;
			break;
		case 2400:
			spd = B2400;
			break;
		case 4800:
			spd = B4800;
			break;
		case 9600:
			spd = B9600;
			break;
		case 19200:
			spd = B19200;
			break;
		case 38400:
			spd = B38400;
			break;
		case 57600:
			spd = B57600;
			break;
		case 115200:
			spd = B115200;
			break;
#ifdef HIGH_BAUD
		case 230400:
			spd = B230400;
			break;
		case 460800:
			spd = B460800;
			break;
		case 921600:
			spd = B921600;
			break;
#endif
		default:
			term_errno = TERM_EBAUD;
			rval = -1;
			break;
		}
		if ( rval < 0 ) break;

		r = cfsetospeed(&tio, spd);
		if ( r < 0 ) {
			term_errno = TERM_ESETOSPEED;
			rval = -1;
			break;
		}
			
		r = cfsetispeed(&tio, spd);
		if ( r < 0 ) {
			term_errno = TERM_ESETISPEED;
			rval = -1;
			break;
		}

		term.nexttermios[i] = tio;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_parity (int fd, enum parity_e parity) 
{
	int rval, i;
	struct termios *tiop;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		tiop = &term.nexttermios[i];

		switch (parity) {
		case P_EVEN:
			tiop->c_cflag &= ~PARODD;
			tiop->c_cflag |= PARENB;
			break;
		case P_ODD:
			tiop->c_cflag |= PARENB | PARODD;
			break;
		case P_NONE:
			tiop->c_cflag &= ~(PARENB | PARODD); 
			break;
		default:
			term_errno = TERM_EPARITY;
			rval = -1;
			break;
		}
		if ( rval < 0 ) break;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_databits (int fd, int databits)
{
	int rval, i;
	struct termios *tiop;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		tiop = &term.nexttermios[i];
				
		switch (databits) {
		case 5:
			tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS5;
			break;
		case 6:
			tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS6;
			break;
		case 7:
			tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS7;
			break;
		case 8:
			tiop->c_cflag = (tiop->c_cflag & ~CSIZE) | CS8;
			break;
		default:
			term_errno = TERM_EDATABITS;
			rval = -1;
			break;
		}
		if ( rval < 0 ) break;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_flowcntrl (int fd, enum flowcntrl_e flowcntl)
{
	int rval, i;
	struct termios *tiop;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}
		
		tiop = &term.nexttermios[i];

		switch (flowcntl) {
		case FC_RTSCTS:
			tiop->c_cflag |= CRTSCTS;
			tiop->c_iflag &= ~(IXON | IXOFF | IXANY);
			break;
		case FC_XONXOFF:
			tiop->c_cflag &= ~(CRTSCTS);
			tiop->c_iflag |= IXON | IXOFF;
			break;
		case FC_NONE:
			tiop->c_cflag &= ~(CRTSCTS);
			tiop->c_iflag &= ~(IXON | IXOFF | IXANY);
			break;
		default:
			term_errno = TERM_EFLOW;
			rval = -1;
			break;
		}
		if ( rval < 0 ) break;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_local(int fd, int local)
{
	int rval, i;
	struct termios *tiop;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		tiop = &term.nexttermios[i];

		if ( local )
			tiop->c_cflag |= CLOCAL;
		else
			tiop->c_cflag &= ~CLOCAL;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set_hupcl (int fd, int on)
{
	int rval, i;
	struct termios *tiop;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

		tiop = &term.nexttermios[i];

		if ( on )
			tiop->c_cflag |= HUPCL;
		else
			tiop->c_cflag &= ~HUPCL;

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_set(int fd,
		 int raw,
		 int baud, enum parity_e parity, int bits, enum flowcntrl_e fc,
		 int local, int hup_close)
{
	int rval, r, i, ni;
	struct termios tio;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			ni = term_add(fd);
			if ( ni < 0 ) {
				rval = -1;
				break;
			}
		} else {
			ni = i;
		}

		tio = term.nexttermios[ni];

		do { /* dummy */

			if (raw) {
				r = term_set_raw(fd);
				if ( r < 0 ) { rval = -1; break; }
			}
			
			r = term_set_baudrate(fd, baud);
			if ( r < 0 ) { rval = -1; break; }
			
			r = term_set_parity(fd, parity);
			if ( r < 0 ) { rval = -1; break; }
			
			r = term_set_databits(fd, bits);
			if ( r < 0 ) { rval = -1; break; }
			
			r = term_set_flowcntrl(fd, fc);
			if ( r < 0 ) { rval = -1; break; }
			
			r = term_set_local(fd, local);
			if ( r < 0 ) { rval = -1; break; }
			
			r = term_set_hupcl(fd, hup_close);
			if ( r < 0 ) { rval = -1; break; }
			
		} while (0);

		if ( rval < 0 ) { 
			if ( i < 0 )
				/* new addition. must be removed */
				term.fd[ni] = -1;
			else
				/* just revert to previous settings */
				term.nexttermios[ni] = tio;
		}

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_pulse_dtr (int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

#ifdef __linux__
		{
			int opins = TIOCM_DTR;

			r = ioctl(fd, TIOCMBIC, &opins);
			if ( r < 0 ) {
				term_errno = TERM_EDTRDOWN;
				rval = -1;
				break;
			}

			sleep(1);

			r = ioctl(fd, TIOCMBIS, &opins);
			if ( r < 0 ) {
				term_errno = TERM_EDTRUP;
				rval = -1;
				break;
			}
		}
#else
		{
			struct termios tio, tioold;

			r = tcgetattr(fd, &tio);
			if ( r < 0 ) {
				term_errno = TERM_ESETATTR;
				rval = -1;
				break;
			}
			
			tioold = tio;
			
			cfsetospeed(&tio, B0);
			cfsetispeed(&tio, B0);
			r = tcsetattr(fd, TCSANOW, &tio);
			if ( r < 0 ) {
				term_errno = TERM_ESETATTR;
				rval = -1;
				break;
			}
			
			sleep(1);
			
			r = tcsetattr(fd, TCSANOW, &tioold);
			if ( r < 0 ) {
				term.currtermios[i] = tio;
				term_errno = TERM_ESETATTR;
				rval = -1;
				break;
			}
		}
#endif /* of __linux__ */
			
	} while (0);

	return rval;
}

/***************************************************************************/

int
term_raise_dtr(int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) {
			rval = -1;
			break;
		}

#ifdef __linux__
		{
			int opins = TIOCM_DTR;

			r = ioctl(fd, TIOCMBIS, &opins);
			if ( r < 0 ) {
				term_errno = TERM_EDTRUP;
				rval = -1;
				break;
			}
		}
#else
		r = tcsetattr(fd, TCSANOW, &term.currtermios[i]);
		if ( r < 0 ) {
			/* FIXME: perhaps try to update currtermios */
			term_errno = TERM_ESETATTR;
			rval = -1;
			break;
		}
#endif /* of __linux__ */
	} while (0);

	return rval;
}

/***************************************************************************/


int
term_lower_dtr(int fd)
{
	int rval, r, i;

	rval = 0;

	do { /* dummy */

		i = term_find(fd);
		if ( i < 0 ) { 
			rval = -1;
			break;
		}

#ifdef __linux__
		{
			int opins = TIOCM_DTR;

			r = ioctl(fd, TIOCMBIC, &opins);
			if ( r < 0 ) {
				term_errno = TERM_EDTRDOWN;
				rval = -1;
				break;
			}
		}
#else
		{
			struct termios tio;

			r = tcgetattr(fd, &tio);
			if ( r < 0 ) {
				term_errno = TERM_EGETATTR;
				rval = -1;
				break;
			}
			term.currtermios[i] = tio;
			
			cfsetospeed(&tio, B0);
			cfsetispeed(&tio, B0);
			
			r = tcsetattr(fd, TCSANOW, &tio);
			if ( r < 0 ) {
				term_errno = TERM_ESETATTR;
				rval = -1;
				break;
			}
		}
#endif /* of __linux__ */
	} while (0);
	
	return rval;
}

/***************************************************************************/

int
term_drain(int fd)
{
	int rval, r;

	rval = 0;

	do { /* dummy */

		r = term_find(fd);
		if ( r < 0 ) {
			rval = -1;
			break;
		}

		do {
			r = tcdrain(fd);
		} while ( r < 0 && errno == EINTR);
		if ( r < 0 ) {
			term_errno = TERM_EDRAIN;
			rval = -1;
			break;
		}

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_flush(int fd)
{
	int rval, r;

	rval = 0;

	do { /* dummy */

		r = term_find(fd);
		if ( r < 0 ) {
			rval = -1;
			break;
		}

		r = tcflush(fd, TCIOFLUSH);
		if ( r < 0 ) {
			rval = -1;
			break;
		}

	} while (0);

	return rval;
}

/***************************************************************************/

int
term_break(int fd)
{
	int rval, r;

	rval = 0;

	do { /* dummy */

		r = term_find(fd);
		if ( r < 0 ) {
			rval = -1;
			break;
		}
	
		r = tcsendbreak(fd, 0);
		if ( r < 0 ) {
			term_errno = TERM_EBREAK;
			rval = -1;
			break;
		}

	} while (0);

	return rval;
}

/**************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
