/*
 * termios2.c
 *
 * Use termios2 interface to set custom baud rates to serial ports.
 *
 * by Nick Patavalis (npat@efault.net)
 *
 * ATTENTION: Very linux-specific kludge!
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

#ifndef __linux__
#error "Linux specific code!"
#endif /* of __linux__ */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

/* These definitions must correspond to the kernel structures as
   defined in:

     <linux-kernel>/arch/<arch>/include/uapi/asm/termbits.h
     or <linux-kernel>/include/uapi/asm-generic/termbits.h

   which is the same as:

     /usr/include/<arch>/asm/termbits.h
     or /usr/include/asm-generic/termbits.h

  Unfortunatelly, we cannot just include <asm/termbits.h> or
  <asm/termios.h> or <linux/termios.h> (all would do the trick)
  because then "struct termios" would be re-defined to the kernel
  version, which is not the same as the libc version. In effect, you
  cannot both include <termios.h> and <linux/termios.h> because both
  define a "struct termios" which may or maynot be the same. We want
  our "struct termios" here to be the libc version (as defined in
  <termios.h>), because that's what our callers use. As a result we
  cannot get the definion of "struct termios2" from the above header
  files since this would also bring-in the clashing definition of the
  kernel version of "struct termios". If you have an idea for a better
  way out of this mess, I would REALLY like to hear it.
*/

/* These (especially NCCS) *may* need to be ifdef'ed according to
   architecture. */

#define K_NCCS 19
struct termios2 {
        tcflag_t c_iflag;               /* input mode flags */
        tcflag_t c_oflag;               /* output mode flags */
        tcflag_t c_cflag;               /* control mode flags */
        tcflag_t c_lflag;               /* local mode flags */
        cc_t c_line;                    /* line discipline */
        cc_t c_cc[K_NCCS];              /* control characters */
        speed_t c_ispeed;               /* input speed */
        speed_t c_ospeed;               /* output speed */
};

#define BOTHER 0010000
#define IBSHIFT 16

/* GLIBC termios use an (otherwise unused) bit in c_iflags to
   internally record the fact that ispeed was set to zero (which is
   special behavior and means "same as ospeed". We want to clear this
   bit before passing c_iflags back to the kernel. See: 
  
       <glibc-source>/sysdeps/unix/sysv/linux/speed.c 
*/
#define IBAUD0 020000000000

int
tc2setattr(int fd, int optional_actions, const struct termios *tios)
{
	struct termios2 t2;
	int cmd;

	switch (optional_actions) {
	case TCSANOW:
		cmd = TCSETS2;
		break;
	case TCSADRAIN:
		cmd = TCSETSW2;
		break;
	case TCSAFLUSH:
		cmd = TCSETSF2;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	t2.c_iflag = tios->c_iflag & ~IBAUD0;
	t2.c_oflag = tios->c_oflag;
	t2.c_cflag = tios->c_cflag;
	t2.c_lflag = tios->c_lflag;
	t2.c_line = tios->c_line;
	t2.c_ispeed = tios->c_ispeed;
	t2.c_ospeed = tios->c_ospeed;
	memcpy(&t2.c_cc[0], &tios->c_cc[0], K_NCCS * sizeof (cc_t));
	
	return ioctl(fd, cmd, &t2);
}

int
tc2getattr(int fd, struct termios *tios)
{
	struct termios2 t2;
	int r;

	r = ioctl(fd, TCGETS2, &t2);
	if (r < 0) return r;

	tios->c_iflag = t2.c_iflag & ~IBAUD0;
	tios->c_oflag = t2.c_oflag;
	tios->c_cflag = t2.c_cflag;
	tios->c_lflag = t2.c_lflag;
	tios->c_line = t2.c_line;
	tios->c_ispeed = t2.c_ispeed;
	tios->c_ospeed = t2.c_ospeed;
	memcpy(&tios->c_cc[0], &t2.c_cc[0], K_NCCS * sizeof (cc_t));

	return 0;
}

/* The termios2 interface supports separate input and output
   speeds. Old termios supported only one terminal speed. So the
   standard tcsetispeed(3), actually sets the output-speed field, not
   the input-speed field (or does nothing if speed == B0). Use
   tc2setispeed if you want to set a standard input speed (one of the
   Bxxxxx speeds) that may be different from the output speed. Also if
   someone, somehow, has set the input speed to something other than
   B0, then you *must* use c2setispeed() to change it. Using the
   standard cfsetispeed() obviously won't do (since it affects only
   the output-speed field).
*/

int
cf2setispeed(struct termios *tios, speed_t speed)
{
	if ( (speed & ~CBAUD) != 0 
		 && (speed < B57600 || speed > __MAX_BAUD) ) {
		errno = EINVAL;
		return -1;
	}
	tios->c_ispeed = speed;
	tios->c_cflag &= ~((CBAUD | CBAUDEX) << IBSHIFT);
	tios->c_cflag |= (speed << IBSHIFT);

	return 0;
}

speed_t
cf2getispeed(struct termios *tios)
{
	return (tios->c_cflag >> IBSHIFT) & (CBAUD | CBAUDEX);
}

int
cf2setospeed_custom(struct termios *tios, int speed)
{
	if ( speed <= 0 ) {
		errno = EINVAL;
		return -1;
	}		
	tios->c_cflag = (tios->c_cflag & ~CBAUD) | BOTHER;
	tios->c_ospeed = speed;

	return 0;
}

int
cf2setispeed_custom(struct termios *tios, int speed)
{
	if ( speed < 0 ) {
		errno = EINVAL;
		return -1;
	}
	if ( speed == 0 ) {
		/* Special case: ispeed == 0 means "same as ospeed". Kernel
		 * does this if it sees B0 in the "CIBAUD" field (i.e. in
		 * CBAUD << IBSHIFT) */
		tios->c_cflag = 
			(tios->c_cflag & ~(CBAUD << IBSHIFT)) | (B0 << IBSHIFT);
	} else {
		tios->c_cflag = 
			(tios->c_cflag & ~(CBAUD << IBSHIFT)) | (BOTHER << IBSHIFT);
		tios->c_ispeed = speed;
	}

	return 0;
}

/* Helpers for debugging */

void
cf2showspeed(struct termios *tios)
{
	printf("CB: %08o, CIB: %08o, ospeed = %d, ispeed = %d\r\n",
		   tios->c_cflag & (CBAUD | CBAUDEX),
		   (tios->c_cflag >> IBSHIFT) & (CBAUD | CBAUDEX),
		   tios->c_ospeed,
		   tios->c_ispeed);
}

void 
tc2showspeed(int fd) 
{
	struct termios tios;
	int r;

	r = tc2getattr(fd, &tios);
	if (r < 0) {
		printf("%d: Failed to get attrs: %s", fd, strerror(errno));
		return;
	}

	printf("%d: ", fd);
	cf2showspeed(&tios);
}
