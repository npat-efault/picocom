#ifndef TN2217_H
#define TN2217_H

/* Open a socket to a telnet service.
 * The port string will be of the form "<hostname>[,<port>]".
 * If the port is omitted, uses "telnet" from /etc/servcies.
 * The tn2217_ops should be used to interact with the returned fd.
 * Returns -1 on failure, and prints an error message to stderr.  */
int tn2217_open(const char *port);

struct term_ops;
extern const struct term_ops tn2217_ops;

#endif /* of TN2217_H */
