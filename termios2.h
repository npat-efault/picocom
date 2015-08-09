/*
 * termios2.h
 *
 * Use termios2 interface to set custom baud rates to serial ports.
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

#ifndef TERMIOS2_H
#define TERMIOS2_H

#include <termios.h>

/* Replacements for the standard tcsetattr(3), tcgetattr(3)
 * functions. Same interface, but these use the new termios2 kernel
 * interface (new ioctl's) which allow custom baud-rate
 * definitions. */

int tc2setattr(int fd, int optional_actions, const struct termios *tios);
int tc2getattr(int fd, struct termios *tios);

/* Replacements for the standard cfgetispeed(3), cfsetispeed(3)
 * functions. Use these to set / get standard *input* baudrates. You
 * can still use cfgetospeed(3), cfsetospeed(3) to set / get the
 * standard output baudrates. The new termios2 interface, unlike the
 * old one, supports different input and output speeds for a
 * device. The "speed" argument must be (and the return value will be)
 * one of the standard "Bxxxx" macros. If cf2getispeed() or
 * cfgetospeed(3) return CBAUDEX, then the respective baudrate is a
 * custom one. Read the "termios.c_ispeed" / "termios.c_ospeed" fields
 * to get the custom value (as a numeric speed). */

int cf2setispeed(struct termios *tios, speed_t speed);
speed_t cf2getispeed(struct termios *tios);

/* Use these to set *custom* input and output baudrates for a
 * device. The "speed" argument must be a numeric baudrate value
 * (e.g. 1234 for 1234 bps). */

int cf2setispeed_custom(struct termios *tios, int speed);
int cf2setospeed_custom(struct termios *tios, int speed);

/* Helpers for debugging */

void cf2showspeed(struct termios *tios);
void tc2showspeed(int fd) ;


#endif /* of TERMIOS2_H */

