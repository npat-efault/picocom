/* vi: set sw=4 ts=4:
 *
 * custbaud.c
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
#include <errno.h>
#include "custbaud.h"

#ifndef USE_CUSTOM_BAUD

int use_custom_baud() { return 0; }

int cfsetispeed_custom(struct termios *tios, int speed) { errno = EINVAL; return -1; }
int cfsetospeed_custom(struct termios *tios, int speed) { errno = EINVAL; return -1; }
int cfgetispeed_custom(const struct termios *tios) { errno = EINVAL; return -1; }
int cfgetospeed_custom(const struct termios *tios) { errno = EINVAL; return -1; }

#else /* USE_CUSTOM_BAUD */

int
use_custom_baud()
{
#ifdef __linux__
    static int use = -1;
    if ( use < 0 )
        use = getenv("NO_CUSTOM_BAUD") ? 0 : 1;
    return use;
#else
    return 1;
#endif
}

#endif /* of ndef USE_CUSTOM_BAUD */

/**************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
