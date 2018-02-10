/* vi: set sw=4 ts=4:
 *
 * term.h
 *
 * Simple terminal management library. Wraps termios(3), and
 * simplifies the logistics required for the reliable management and
 * control of terminals.
 *
 * Principles of operation:
 *
 * After the library is initialized, one or more file-descriptors can
 * be added to (and latter removed from) the list managed by the
 * it. These file descriptors must be opened on terminal devices. For
 * every fd, the original settings of the associated terminal device
 * are saved by the library. These settings are restored when the fd
 * is removed from the framework, or at program termination [by means
 * of an atexit(3) handler installed by the library], or at user
 * request. The library maintains three structures for every fd in the
 * framework: The original settings structure ("origtermios"), keeping
 * the settings of the terminal device when the respective filedes was
 * added to the framework. The current settings structure
 * ("currtermios"), keeping the current settings of the associated
 * terminal device; and the next settings structure ("nexttermios")
 * which keeps settings to be applied to the associated terminal
 * device at a latter time, upon user request.  The "term_set_*"
 * functions can be used to modify the device settings stored in the
 * nexttermios structure. Using functions provided by the library the
 * user can: Apply the nexttermios settings to the device. Revert all
 * changes made on nexttermios by copying the currtermios structure to
 * nexttermios. Reset the device, by configuring it to the original
 * settings, and copying origtermios to currtermios and
 * nexttermios. Refresh the device by rereading the current settings
 * from it and updating currtermios (to catch up with changes made to
 * the device by means outside of this framework).
 *
 * Interface summary:
 *
 * F term_lib_init  - library initialization
 * F term_add - add a filedes to the framework
 * F term_remove - remove a filedes from the framework
 * F term_erase - remove a filedes from the framework without reset
 * F term_replace - replace a fd w/o affecting the settings stuctures
 * F term_reset - revert a device to the settings in "origtermios"
 * F term_apply - configure a device to the settings in "nexttermios"
 * F term_revert - discard "nexttermios" by copying-over "currtermios"
 * F term_refresh - update "currtermios" from the device
 * F term_set_raw - set "nexttermios" to raw mode
 * F term_set_baudrate - set the baudrate in "nexttermios"
 * F term_set_parity - set the parity mode in "nexttermios"
 * F term_set_databits - set the databits in "nexttermios"
 * F term_set_stopbits - set the stopbits in "nexttermios"
 * F term_set_flowcntrl - set the flowcntl mode in "nexttermios"
 * F term_set_hupcl - enable or disable hupcl in "nexttermios"
 * F term_set_local - set "nexttermios" to local or non-local mode
 * F term_set - set all params of "nexttermios" in a single stroke
 * F term_get_baudrate - return the baudrate set in "currtermios"
 * F term_get_parity - return the parity setting in "currtermios"
 * F term_get_databits - return the data-bits setting in "currtermios"
 * F term_get_flowcntrl - return the flow-control setting in "currtermios"
 * F term_pulse_dtr - pulse the DTR line a device
 * F term_lower_dtr - lower the DTR line of a device
 * F term_raise_dtr - raise the DTR line of a device
 * F term_lower_rts - lower the RTS line of a device
 * F term_raise_rts - raise the RTS line of a device
 * F term_get_mctl - Get modem control signals status
 * F term_drain - drain the output from the terminal buffer
 * F term_flush - discard terminal input and output queue contents
 * F term_fake_flush - discard terminal input and output queue contents
 * F term_break - generate a break condition on a device
 * F term_baud_up - return next higher baudrate
 * F term_baud_down - return next lower baudrate
 * F term_baud_ok - check if baudrate is valid
 * F term_baud_std - check if baudrate is on of our listed standard baudrates
 * F term_strerror - return a string describing current error condition
 * F term_perror - print a string describing the current error condition
 * G term_errno - current error condition of the library
 * E term_errno_e - error condition codes
 * E parity_t - library supported parity types
 * E flocntrl_t - library supported folw-control modes
 * M MAX_TERM - maximum number of fds that can be managed
 *
 * by Nick Patavalis (npat@efault.net)
 *
 * originaly by Pantelis Antoniou (https://github.com/pantoniou),
 *              Nick Patavalis
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
 * $Id: term.h,v 1.1 2003/05/07 18:00:05 npat Exp $
 */


#ifndef TERM_H
#define TERM_H

/* M MAX_TERMS
 *
 * Maximum nuber of terminals that can be managed by the library. Keep
 * relatively low, since linear searches are used. Reasonable values
 * would be: 16, 32, 64, etc.
 */
#define MAX_TERMS 16

/*
 * E term_errno_e
 *
 * Library error-condition codes. These marked with "see errno"
 * correspond to system errors, so it makes sense to also check the
 * system's error-condition code (errno) in order to fully determine
 * what went wrong.
 *
 * See the error strings in "term.c" for a description of each.
 */
enum term_errno_e {
    TERM_EOK = 0,
    TERM_ENOINIT,
    TERM_EFULL,
    TERM_ENOTFOUND,
    TERM_EEXISTS,
    TERM_EATEXIT,
    TERM_EISATTY,
    TERM_EFLUSH,     /* see errno */
    TERM_EGETATTR,   /* see errno */
    TERM_ESETATTR,   /* see errno */
    TERM_EBAUD,
    TERM_ESETOSPEED,
    TERM_ESETISPEED,
    TERM_EGETSPEED,
    TERM_EPARITY,
    TERM_EDATABITS,
    TERM_ESTOPBITS,
    TERM_EFLOW,
    TERM_EDTRDOWN,
    TERM_EDTRUP,
    TERM_EMCTL,
    TERM_EDRAIN,     /* see errno */
    TERM_EBREAK,
    TERM_ERTSDOWN,
    TERM_ERTSUP
};

/* E parity_e
 *
 * Parity modes supported by the library:
 *
 * P_NONE  - no patiry
 * P_EVEN  - even parity
 * P_ODD   - odd parity
 * P_MARK  - mark parity (parity bit always 1)
 * P_SPACE - space parity (parity bit always 0)
 * P_ERROR - marker to indicate error for functions returning parity_e
 */
enum parity_e {
    P_NONE = 0,
    P_EVEN,
    P_ODD,
    P_MARK,
    P_SPACE,
    P_ERROR
};

/*
 * E flowcntrl_e
 *
 * Flow control modes, supported by the library.
 *
 * FC_NONE - no flow control
 * FC_RTSCTS - RTS/CTS handshaking, also known as hardware
 *     flow-control.
 * FC_XONXOFF  - xon/xoff flow control.
 * FC_ERROR - marker to indicate error for functions returning flowcntrl_e
 */
enum flowcntrl_e {
    FC_NONE = 0,
    FC_RTSCTS,
    FC_XONXOFF,
    FC_OTHER,
    FC_ERROR
};

/*
 * C MCTL_xxx
 *
 * Modem control line bits. Used against the return value of
 * term_get_mctl().
 */
#define MCTL_DTR     (1<<1)  /* O: Data Terminal Ready */
#define MCTL_DSR     (1<<2)  /* I: Data Set Ready */
#define MCTL_DCD     (1<<3)  /* I: Data Carrier Detect */
#define MCTL_RTS     (1<<4)  /* O: Request To Send */
#define MCTL_CTS     (1<<5)  /* I: Clear To Send */
#define MCTL_RI      (1<<6)  /* I: Ring Indicator */
#define MCTL_UNAVAIL (1<<0)  /* MCTL lines (status) not available */

/***************************************************************************/

/*
 * G term_errno
 *
 * Keeps the current library error-condtion code
 */
extern int term_errno;

/***************************************************************************/

/*
 * F term_strerror
 *
 * Return a string descibing the current library error condition.  If
 * the error condition reflects a system error, then the respective
 * system-error description is appended at the end of the returned
 * string. The returned string points to a statically allocated buffer
 * that is overwritten with every call to term_strerror()
 *
 * Returns a string describing the current library (and possibly
 * system) error condition.
 */
const char *term_strerror (int terrnum, int errnum);

/*
 * F term_perror
 *
 * Emit a description of the current library (and possibly system)
 * error condition to the standard-error stream. The description is
 * prefixed by a user-supplied string. What is actually emmited is:
 *
 *     <prefix><space><description>\n
 *
 * The description emitted is the string returned by term_strerror().
 *
 * Returns the number of characters emmited to the standard-error
 * stream or a neagative on failure.
 */
int term_perror (const char *prefix);

/* F term_lib_init
 *
 * Initialize the library
 *
 * Initialize the library. This function must be called before any
 * attemt to use the library. If this function is called and the
 * library is already initialized, all terminals associated with the
 * file-descriptors in the framework will be reset to their original
 * settings, and the file-descriptors will be removed from the
 * framework. An atexit(3) handler is installed by the library which
 * resets and removes all managed terminals.
 *
 * Returns negative on failure, non-negative on success. This function
 * will only fail if the atexit(3) handler cannot be
 * installed. Failure to reset a terminal to the original settings is
 * not considered an error.
 */
int term_lib_init (void);

/* F term_add
 *
 * Add the filedes "fd" to the framework. The filedes must be opened
 * on a terminal device or else the addition will fail. The settings
 * of the terminal device associated with the filedes are read and
 * stored in the origtermios structure.
 *
 * Returns negative on failure, non-negative on success.
 */
int term_add (int fd);

/* F term_remove
 *
 * Remove the filedes "fd" from the framework. The device associated
 * with the filedes is reset to its original settings (those it had
 * when it was added to the framework)
 *
 * Return negative on failure, non-negative on success. The filedes is
 * always removed form the framework even if this function returns
 * failure, indicating that the device reset failed.
 */
int term_remove (int fd);

/* F term_erase
 *
 * Remove the filedes "fd" from the framework. The device associated
 * with the filedes is *not* reset to its original settings.
 *
 * Return negative on failure, non-negative on success. The only
 * reason for failure is the filedes not to be found.
 */
int term_erase (int fd);

/* F term_replace
 *
 * Replace a managed filedes without affecting the associated settings
 * structures. The "newfd" takes the place of "oldfd". "oldfd" is
 * removed from the framework without the associated device beign
 * reset (it is most-likely no longer connected to a device anyway,
 * and reset would fail). The device associated with "newfd" is
 * configured with "oldfd"s current settings (stored in the
 * "currtermios" structure). After applying the settings to "newfd",
 * the "currtermios" structure is re-read from the device, so that it
 * corresponds to the actual device settings.
 *
 * Returns negative on failure, non-negative on success. In case of
 * failure "oldfd" is not removed from the framework, and no
 * replacement takes place.
 *
 * The usual reason to replace the filedes of a managed terminal is
 * because the device was closed and re-opened. This function gives
 * you a way to do transparent "open"s and "close"s: Before you close
 * a device, it has certain settings managed by the library. When you
 * close it and then re-open it many of these settings are lost, since
 * the device reverts to system-default settings. By calling
 * term_replace, you conceptually _maintain_ the old (pre-close)
 * settings to the new (post-open) filedes.
 */
int term_replace (int oldfd, int newfd);

/*
 * F term_apply
 *
 * Applies the settings stored in the "nexttermios" structure
 * associated with the managed filedes "fd", to the respective
 * terminal device.  It then re-reads the settings form the device and
 * stores them in "nexttermios". Finally it copies "nexttermios" to
 * "currtermios". If "now" is not zero, settings are applied
 * immediatelly, otherwise setting are applied after the output
 * buffers are drained and the input buffers are discarder. In this
 * sense, term_apply(fd, 0) is equivalent to: term_drain(fd);
 * term_flush(fd); term_apply(fd, 1);
 *
 * Returns negative on failure, non negative on success. In case of
 * failure the "nexttermios" and "currtermios" structures are not
 * affected.
 */
int term_apply (int fd, int now);

/*
 * F term_revert
 *
 * Discards all the changes made to the nexttermios structure
 * associated with the managed filedes "fd" that have not been applied
 * to the device. It does this by copying currtermios to nexttermios.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_revert (int fd);

/* F term_reset
 *
 * Reset the terminal device associated with the managed filedes "fd"
 * to its "original" settings. This function applies the settings in
 * the "origtermios" structure to the actual device. It then reads the
 * settings from the device and stores them in both the "currtermios"
 * and "nexttermios" stuctures.
 *
 * Returns negative on failure, non-negative of success. On failure
 * the the "origtermios", "currtermios", and "nexttermios" stuctures
 * associated with the filedes remain unaffected.
 */
int term_reset (int fd);

/*
 * F term_refresh
 *
 * Updates the contents of the currtermios structure associated with
 * the managed filedes "fd", by reading the settings from the
 * respective terminal device.
 *
 * Returns negative on failure, non negative on success. On failure
 * the currtermios structure remains unaffected.
 */
int term_refresh (int fd);

/* F term_set_raw
 *
 * Sets the "nexttermios" structure associated with the managed
 * filedes "fd" to raw mode. The effective settings of the device are
 * not affected by this function.
 *
 * Returns negative on failure, non-negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 *
 * When in raw mode, no characters are processed by the terminal
 * driver and there is no line-discipline or buffering. More
 * technically setting to raw mode means, affecting the following
 * terminal settings as indicated:
 *
 *   -ignbrk -brkint -parmrk -istrip -inlcr -igncr -icrnl -ixon
 *   -opost -echo -echonl -icannon -isig -iexten -csize -parenb
 *   cs8 min=1 time=0
 */
int term_set_raw (int fd);

/* F term_set_baudrate
 *
 * Sets the baudrate in the "nexttermios" structure associated with
 * the managed filedes "fd" to "baudrate". The effective settings of
 * the device are not affected by this function.
 *
 * Supported baudrates: 0, 50, 75, 110, 134, 150, 200, 300, 600, 1200,
 *   1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_baudrate (int fd, int baudrate);

/* F term_set_parity
 *
 * Sets the parity mode in the "nexttermios" structure associated with
 * the managed filedes "fd" to "parity". The effective settings of the
 * device are not affected by this function.
 *
 * Supported parity modes are: p_even, p_odd, p_none.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_parity (int fd, enum parity_e parity);

/* F term_set_databits
 *
 * Sets the databits number in the "nexttermios" structure associated
 * with the managed filedes "fd" to "databits". The effective settings
 * of the device are not affected by this function.
 *
 * 5, 6, 7, and 8 databits are supported by the library.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_databits (int fd, int databits);

/* F term_set_stopbits
 *
 * Sets the stopbits number in the "nexttermios" structure associated
 * with the managed filedes "fd" to "stopbits". The effective settings
 * of the device are not affected by this function.
 *
 * 1 and 2 stopbits are supported by the library.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_stopbits (int fd, int stopbits);

/* F term_set_flowcntrl
 *
 * Sets the folwcontrol mode in the "nexttermios" structure associated
 * with the managed filedes "fd" to "flowcntl". The effective settings
 * of the device are not affected by this function.
 *
 * The following flow control modes are supportd by the library:
 * FC_NONE, FC_RTSCTS, FC_XONXOFF.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_flowcntrl (int fd, enum flowcntrl_e flowcntl);

/* F term_set_hupcl
 *
 * Enables ("on" = nonzero) or disables ("on" = zero) the
 * "HUP-on-close" setting in the "nexttermios" structure associated
 * with the managed filedes "fd". The effective settings of the device
 * are not affected by this function.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_hupcl (int fd, int on);

/* F term_set_local.
 *
 * Enables ("local" = nonzero) or disables ("local" = zero) the
 * "local-mode" setting in the "nexttermios" structure associated with
 * the managed filedes "fd". The effective settings of the device are
 * not affected by this function.
 *
 * Returns negative on failure, non negative on success. Returns
 * failure only to indicate invalid arguments, so the return value can
 * be safely ignored.
 */
int term_set_local (int fd, int local);

/* F temr_set
 *
 * Sets most of the parameters in the "nexttermios" structure
 * associated with the managed filedes "fd". Actually sets the
 * following:
 *
 *   Raw mode if "raw" is nonzero.
 *   Baudrate to "baud".
 *   Parity mode to "parity".
 *   Flow control mode to "fc".
 *   Enables local mode if "local" is nonzero, dis. otherwise.
 *   Enables HUP-on-close if "hupcl" is nonzero, dis. otherwise
 *
 * The effective settings of the device are not affected by this
 * function. Additionally if the filedes "fd" is not managed, it is
 * added to the framework.
 *
 * Returns negative on failure, non negative on success. On failure
 * none of the settings of "nexttermios" is affected. *If* the filedes
 * "fd" is already in the framework, then the function returns failure
 * only to indicate invalid arguments, so, in this case, the return
 * value can be safely ignored. If the function successfully adds the
 * filedes to the framework, and following this it fails, then it will
 * remove the filedes before returning.
 */
int term_set (int fd,
              int raw,
              int baud,
              enum parity_e parity,
              int databits, int stopbits,
              enum flowcntrl_e fc,
              int local, int hupcl);

/* F term_get_baudrate
 *
 * Reads and decodes the current baudrate settings in the
 * "currtermios" structure of the managed filedes "fd".
 *
 * Returns the decoded output baudrate (as bits-per-second), or -1 if
 * the output baudrate cannot be decoded, or if "fd" does not
 * correspond to a managed filedes. If "ispeed" is not NULL, it writes
 * the decoded input baudrate to the integer pointed-to by "ispeed";
 * if the input baudrate cannot be decoded in writes -1 instead.
 */
int term_get_baudrate (int fd, int *ispeed);

/* F term_get_parity
 *
 * Reads and decodes the current parity settings in the
 * "currtermios" structure of the managed filedes "fd".
 *
 * Returns one of the "enum parity_e" members. Returns P_ERROR if "fd"
 * does not correspond to a managed filedes.
 */
enum parity_e term_get_parity (int fd);

/* F term_get_databits
 *
 * Reads and decodes the current databits settings in the
 * "currtermios" structure of the managed filedes "fd".
 *
 * Returns the number of databits (5..8), or -1 if "fd" does not
 * correspond to a managed filedes.
 */
int term_get_databits (int fd);

/* F term_get_stopbits
 *
 * Reads and decodes the current stopbits settings in the
 * "currtermios" structure of the managed filedes "fd".
 *
 * Returns the number of databits (1 or 2), or -1 if "fd" does not
 * correspond to a managed filedes.
 */
int term_get_stopbits (int fd);

/* F term_get_flowcntrl
 *
 * Reads and decodes the current flow-control settings in the
 * "currtermios" structure of the managed filedes "fd".
 *
 * Returns one of the "enum flowcntrl_e" members. Returns FC_ERROR if
 * "fd" does not correspond to a managed filedes.
 */
enum flowcntrl_e term_get_flowcntrl (int fd);

/* F term_pulse_dtr
 *
 * Pulses the DTR line of the device associated with the managed
 * filedes "fd". The DTR line is lowered for 1sec and then raised
 * again.
 *
 * Returns negative on failure, non negative on success.
 */
int term_pulse_dtr (int fd);

/* F term_lower_dtr
 *
 * Lowers the DTR line of the device associated with the managed
 * filedes "fd".
 *
 * Returns negative on failure, non negative on success.
 */
int term_lower_dtr (int fd);

/* F term_raise_dtr
 *
 * Raises the DTR line of the device associated with the managed
 * filedes "fd".
 *
 * Returns negative on failure, non negative on success.
 */
int term_raise_dtr (int fd);

/* F term_lower_rts
 *
 * Lowers the RTS line of the device associated with the managed
 * filedes "fd".
 *
 * Returns negative on failure, non negative on success.
 */
int term_lower_rts (int fd);

/* F term_raise_rts
 *
 * Raises the RTS line of the device associated with the managed
 * filedes "fd".
 *
 * Returns negative on failure, non negative on success.
 */
int term_raise_rts (int fd);

/* F term_get_mctl
 *
 * Get the status of the modem control lines of the serial port
 * (terminal) associated with the managed filedes "fd".
 *
 * On error (fd is not managed) return a negative. If the feature is
 * not available returns MCTL_UNAVAIL. Otherwise returns a word that
 * can be checked against the MCTL_* flags.
 */
int term_get_mctl (int fd);

/* F term_drain
 *
 * Drains (flushes) the output queue of the device associated with the
 * managed filedes "fd". This functions blocks until all the contents
 * of output queue have been transmited.
 *
 * Returns negative on failure, non negative on success.
 */
int term_drain (int fd);

/* F term_flush
 *
 * Discards all the contents of the input AND output queues of the
 * device associated with the managed filedes "fd". Although it is
 * called flush this functions does NOT FLUSHES the terminal
 * queues. It just DISCARDS their contents. The name has stuck from
 * the POSIX terminal call: "tcflush".
 *
 * Returns negative on failure, non negative on success.
 */
int term_flush (int fd);

/* F term_fake_flush
 *
 * Fake a term_flush, by temporarily configuring the device associated
 * with the managed fd to no flow-control and waiting until its output
 * queue drains.
 *
 * Returns negative on failure, non-negative on success.
 */
int term_fake_flush(int fd);


/* F term_break
 *
 * This function generates a break condition on the device associated
 * with the managed filedes "fd", by transmiting a stream of
 * zero-bits. The stream of zero-bits has a duriation typically
 * between 0.25 and 0.5 seconds.
 *
 * Returns negative on failure, non negative on success.
 */
int term_break(int fd);

/***************************************************************************/

/* F term_baud_up
 *
 * Returns the next higher valid baudrate. Returns "baud" if there is
 * no higher valid baudrate.
 */
int term_baud_up (int baud);


/* F term_baud_down
 *
 * Returns the next lower valid baudrate. Returns "baud" if there is
 * no lower valid baudrate.
 */
int term_baud_down (int baud);

/* F term_baud_ok
 *
 * Returns non-zero if "baud" is a valid baudrate, zero otherwise.
 */
int term_baud_ok(int baud);

/* F term_baud_std
 *
 * Returns non-zero if "baud" is a standard baudrate, zero otherwise.
 */
int term_baud_std(int baud);

/***************************************************************************/

#endif /* of TERM_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
