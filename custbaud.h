/* vi: set sw=4 ts=4:
 *
 * custbaud.h
 *
 * Automatically enable custom baudrate support for systems (OS /
 * version / architecture combinations) we know it works.
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

#ifndef CUSTBAUD_H
#define CUSTBAUD_H

#include <termios.h>

#ifndef NO_CUSTOM_BAUD

#if defined (__linux__)

/* Enable by-default for kernels > 2.6.0 on x86 and x86_64 only */
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
/* Some libc implementations (e.g. musl) do not define the cispeed and
   cospeed struct termios fields. We do not support custom baudrates
   on them. */
#if ( (defined (__i386__) || defined (__x86_64__))  \
      && defined (_HAVE_STRUCT_TERMIOS_C_ISPEED)    \
      && defined (_HAVE_STRUCT_TERMIOS_C_OSPEED) )  \
    || defined (USE_CUSTOM_BAUD)
#ifndef USE_CUSTOM_BAUD
#define USE_CUSTOM_BAUD
#endif
#define CUSTOM_BAUD_HEAD "termios2.h"
#endif /* of arch */
#endif /* of version */

#elif defined (__APPLE__) && defined(__MACH__)

#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR
/* Do not enable by default for iOS in Xcode simulator */
#elif TARGET_OS_IPHONE
/* Do not enable by default for iOS until it has been tested */
#elif TARGET_OS_MAC
#if defined (__i386__) || defined (__x86_64__)
/* Enable by-default for Intel Mac, macOS / OSX >= 10.4 (Tiger) */
#ifndef USE_CUSTOM_BAUD
#define USE_CUSTOM_BAUD
#endif
#endif /* of arch */
#endif /* of TARGET_OS_... */
#define CUSTOM_BAUD_HEAD "custbaud_bsd.h"

#elif defined (__NetBSD__)

/* Do not enable by default */
#define CUSTOM_BAUD_HEAD "custbaud_bsd.h"

#elif defined (__FreeBSD__) || defined (__OpenBSD__) || \
      defined (__DragonFly__)

/* I believe it doesn't hurt to enable by-default for these */
#ifndef USE_CUSTOM_BAUD
#define USE_CUSTOM_BAUD
#endif
#define CUSTOM_BAUD_HEAD "custbaud_bsd.h"

#elif defined (USE_CUSTOM_BAUD)

#error "USE_CUSTOM_BAUD not supported on this system!"

#endif /* of platforms */

#else /* of ndef NO_CUSTOM_BAUD */

#ifdef USE_CUSTOM_BAUD
#undef USE_CUSTOM_BAUD
#endif

#endif /* of ndef NO_CUSTOM_BAUD else */


int use_custom_baud();
int cfsetispeed_custom(struct termios *tios, int speed);
int cfsetospeed_custom(struct termios *tios, int speed);
int cfgetispeed_custom(const struct termios *tios);
int cfgetospeed_custom(const struct termios *tios);

#endif /* CUSTBAUD_H */

/**************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
