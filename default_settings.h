#ifndef DEFAULT_SETTINGS_H
#define DEFAULT_SETTINGS_H

#ifndef DEF_PORT
#define DEF_PORT "/dev/ttyS0"
#endif

#ifndef DEF_BAUD
#define DEF_BAUD 9600
#endif

#ifndef DEF_FLOW
#define DEF_FLOW FC_NONE
#endif

#ifndef DEF_PARITY
#define DEF_PARITY P_NONE
#endif

#ifndef DEF_DATABITS
#define DEF_DATABITS 8
#endif

#ifndef DEF_STOPBITS
#define DEF_STOPBITS 1
#endif

#ifndef DEF_LECHO
#define DEF_LECHO 0
#endif

#ifndef DEF_NOINIT
#define DEF_NOINIT 0
#endif

#ifndef DEF_NORESET
#define DEF_NORESET 0
#endif

#ifndef DEF_HANGUP
#define DEF_HANGUP 0
#endif

#if defined (UUCP_LOCK_DIR) || defined (USE_FLOCK)

#ifndef DEF_NOLOCK
#define DEF_NOLOCK 0
#endif

#endif

#ifndef DEF_ESCAPE
#define DEF_ESCAPE 'a'
#endif

#ifndef DEF_NOESCAPE
#define DEF_NOESCAPE 0
#endif

#ifndef DEF_SEND_CMD
#define DEF_SEND_CMD "sz -vv"
#endif

#ifndef DEF_RECEIVE_CMD
#define DEF_RECEIVE_CMD "rz -vv"
#endif

#ifndef DEF_IMAP
#define DEF_IMAP M_I_DFL
#endif

#ifndef DEF_OMAP
#define DEF_OMAP M_O_DFL
#endif

#ifndef DEF_EMAP
#define DEF_EMAP M_E_DFL
#endif

#ifndef DEF_LOG_FILENAME
#define DEF_LOG_FILENAME NULL
#endif

#ifndef DEF_INITSTRING
#define DEF_INITSTRING NULL
#endif

#ifndef DEF_EXIT_AFTER
#define DEF_EXIT_AFTER -1
#endif

#ifndef DEF_EXIT
#define DEF_EXIT 0
#endif

#ifndef DEF_LOWER_RTS
#define DEF_LOWER_RTS 0
#endif

#ifndef DEF_LOWER_DTR
#define DEF_LOWER_DTR 0
#endif

#ifndef DEF_RAISE_RTS
#define DEF_RAISE_RTS 0
#endif

#ifndef DEF_RAISE_DTR
#define DEF_RAISE_DTR 0
#endif

#ifndef DEF_QUIET
#define DEF_QUIET 0
#endif

#endif
