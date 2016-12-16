/*
 * termbits2.c
 *
 * Stuff that we should include from kernel sources, if we could; but
 * we can't. Included from "termios2.h"
 *
 * by Nick Patavalis (npat@efault.net)
 *
 * ATTENTION: Linux-specific kludge!
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

#ifndef TERMBITS2_H
#define TERMBITS2_H

#ifndef __linux__
#error "Linux specific code!"
#endif

/* We need tcflag_t, cc_t, speed_t, CBAUDEX, etc */
#include <termios.h>

/* These definitions must correspond to the kernel structures as
   defined in:

     <linux-kernel>/arch/<arch>/include/uapi/asm/termbits.h
     or <linux-kernel>/include/uapi/asm-generic/termbits.h

   which are the same as:

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
  files, since this would also bring-in the clashing definition of the
  kernel version of "struct termios". If you have an idea for a better
  way out of this mess, I would REALLY like to hear it.

  I hope that soon GLIBC will pick-up termios2 and all these will be
  useless. Until then ...

  ATTENTION: For most architectures "struct termios2" and the
  associated constants we care about (NCCS, BOTHER, IBSHIFT) are the
  same. For some there are small differences, and some architectures
  do not support termios2 at all. I don't claim to have done a
  thorough job figuring out the specifics for every architecture, so
  your milleage may vary. In any case, if you want support for
  something that's missing, just copy the relevant definitions from
  the kernel header file in here, recompile, test, and send me a
  patch. */

#if defined (__alpha__)

#error "Architecure has no termios2 support"


#elif defined (__powerpc__) || defined (__powerpc64__)

#define K_NCCS 19
/* The "old" termios is the same with termios2 for powerpc's */
struct termios2 {
        tcflag_t c_iflag;               /* input mode flags */
        tcflag_t c_oflag;               /* output mode flags */
        tcflag_t c_cflag;               /* control mode flags */
        tcflag_t c_lflag;               /* local mode flags */
        cc_t c_cc[K_NCCS];              /* control characters */
        cc_t c_line;                    /* line discipline */
        speed_t c_ispeed;               /* input speed */
        speed_t c_ospeed;               /* output speed */
};

#define BOTHER 00037
#define IBSHIFT 16

/* powerpc ioctl numbers have the argument-size encoded. Make sure we
   use the correct structure (i.e. kernel termios, not LIBC termios)
   when calculating them. */
#define IOCTL_SETS  _IOW('t', 20, struct termios2)
#define IOCTL_SETSW _IOW('t', 21, struct termios2)
#define IOCTL_SETSF _IOW('t', 22, struct termios2)
#define IOCTL_GETS  _IOR('t', 19, struct termios2)


#elif defined (__mips__)

#define K_NCCS 23
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

#define BOTHER  CBAUDEX
#define IBSHIFT 16

#define IOCTL_SETS TCSETS2
#define IOCTL_SETSW TCSETSW2
#define IOCTL_SETSF TCSETSF2
#define IOCTL_GETS TCGETS2


#else /* All others */

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

#define BOTHER CBAUDEX
#define IBSHIFT 16

#define IOCTL_SETS TCSETS2
#define IOCTL_SETSW TCSETSW2
#define IOCTL_SETSF TCSETSF2
#define IOCTL_GETS TCGETS2

#endif /* of architectures */

/***************************************************************************/

#endif /* of TERMBITS2_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
