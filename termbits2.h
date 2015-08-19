/*
 * termbits2.c
 *
 * Stuff that we should include from kernel source, if we could.
 * Included from termios2.h
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

#ifndef TERMBITS2_H
#define TERMBITS2_H

/* We need tcflag_t, cc_t, speed_t, CBAUDEX, etc */
#include <termios.h>

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

/* K_NCCS *may* need to be ifdef'ed according to architecture. A quick
   grep in the kernel sources (3.x) reveals that, most likely, only
   the MIPS arch has a different NCCS value.
*/
#if defined __mips__
#define K_NCCS 23
#else
/* All others */
#define K_NCCS 19
#endif

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


/* BOTHER needs to be ifdef'ed according to architecture. A quick grep
   in the kernel sources (3.x) reveals that, most likely, only the
   PowerPC arch has a different BOTHER value.
*/
#if defined __powerpc__
#define BOTHER 0037
#else
/* All others */
#define BOTHER CBAUDEX
#endif

/* AFAIK this is the same on all architectures */
#define IBSHIFT 16

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
