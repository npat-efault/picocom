/*
 * termios2.h
 *
 * Use termios2 interface to set custom baud rates to serial ports.
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

#ifndef TERMIOS2_H
#define TERMIOS2_H

#include <termios.h>

/* Replace termios functions, with termios2 functions */
#define tcsetattr tc2setattr
#define tcgetattr tc2getattr
#define cfsetispeed cf2setispeed
#define cfgetispeed cf2getispeed

/* And define these new ones */
#define cfsetospeed_custom cf2setospeed_custom
#define cfsetispeed_custom cf2setispeed_custom
#define cfgetospeed_custom(tiop) ((tiop)->c_ospeed)
#define cfgetispeed_custom(tiop) ((tiop)->c_ispeed)

/* Replacements for the standard tcsetattr(3), tcgetattr(3)
 * functions. Same user interface, but these use the new termios2
 * kernel interface (new ioctl's) which allow custom baud-rate
 * setting. */

int tc2setattr(int fd, int optional_actions, const struct termios *tios);
int tc2getattr(int fd, struct termios *tios);

/* Replacements for the standard cfgetispeed(3), cfsetispeed(3)
 * functions. Use these to set / get standard *input* baudrates. You
 * can still use cfgetospeed(3), cfsetospeed(3) to set / get the
 * standard output baudrates. The new termios2 interface, unlike the
 * old one, supports different input and output speeds for a
 * device. The "speed" argument must be (and the return value will be)
 * one of the standard "Bxxxx" macros. If cf2getispeed() or
 * cfgetospeed(3) return BOTHER, then the respective baudrate is a
 * custom one. Read the "termios.c_ispeed" / "termios.c_ospeed" fields
 * to get the custom value (as a numeric speed). */

int cf2setispeed(struct termios *tios, speed_t speed);
speed_t cf2getispeed(const struct termios *tios);

/* Use these to set *custom* input and output baudrates for a
 * device. The "speed" argument must be a numeric baudrate value
 * (e.g. 1234 for 1234 bps). */

int cf2setispeed_custom(struct termios *tios, int speed);
int cf2setospeed_custom(struct termios *tios, int speed);

/***************************************************************************/

#endif /* of TERMIOS2_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
