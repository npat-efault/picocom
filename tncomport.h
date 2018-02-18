/* vi: set sw=4 ts=4:
 *
 * tncomport.h
 *
 * RFC2217 COM-PORT-OPTION constant definitions
 *
 * by David Leonard (https://github.com/dleonard0)
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

#ifndef TNCOMPORT_H
#define TNCOMPORT_H

#define TELOPT_COMPORT        44

/* Suboption codes */
#define COMPORT_SIGNATURE           0  /* <text> */
#define COMPORT_SET_BAUDRATE        1  /* <value(4)> */
#define COMPORT_SET_DATASIZE        2  /* <value> */
#define COMPORT_SET_PARITY          3  /* <value> */
#define COMPORT_SET_STOPSIZE        4  /* <value> */
#define COMPORT_SET_CONTROL         5  /* <value> */
#define COMPORT_NOTIFY_LINESTATE    6  /* <value> */
#define COMPORT_NOTIFY_MODEMSTATE   7  /* <value> */
#define COMPORT_FLOWCONTROL_SUSPEND 8  /* replaces RFC1372 */
#define COMPORT_FLOWCONTROL_RESUME  9  /* replaces RFC1372 */
#define COMPORT_SET_LINESTATE_MASK  10  /* <value> */
#define COMPORT_SET_MODEMSTATE_MASK 11  /* <value> */
#define COMPORT_PURGE_DATA          12  /* <value> */

#define COMPORT_SERVER_BASE         100 /* server message offset */

#define COMPORT_BAUDRATE_REQUEST    0   /* max baudrate is 4.2 Gbaud */

#define COMPORT_DATASIZE_REQUEST    0
#define COMPORT_DATASIZE_5          5
#define COMPORT_DATASIZE_6          6
#define COMPORT_DATASIZE_7          7
#define COMPORT_DATASIZE_8          8

#define COMPORT_PARITY_REQUEST      0
#define COMPORT_PARITY_NONE         1
#define COMPORT_PARITY_ODD          2
#define COMPORT_PARITY_EVEN         3
#define COMPORT_PARITY_MARK         4
#define COMPORT_PARITY_SPACE        5

#define COMPORT_STOPSIZE_REQUEST    0
#define COMPORT_STOPSIZE_1          1
#define COMPORT_STOPSIZE_2          2
#define COMPORT_STOPSIZE_1_5        3   /* when datasize = 5 */

#define COMPORT_CONTROL_FC_REQUEST  0   /* request FC state */
#define COMPORT_CONTROL_FC_NONE     1
#define COMPORT_CONTROL_FC_XONOFF   2
#define COMPORT_CONTROL_FC_HARDWARE 3
#define COMPORT_CONTROL_FC_DCD      17
#define COMPORT_CONTROL_FC_DSR      19
#define COMPORT_CONTROL_BREAK_REQUEST 4 /* request BREAK state */
#define COMPORT_CONTROL_BREAK_ON    5
#define COMPORT_CONTROL_BREAK_OFF   6
#define COMPORT_CONTROL_DTR_REQUEST 7   /* request DTR state */
#define COMPORT_CONTROL_DTR_ON      8
#define COMPORT_CONTROL_DTR_OFF     9
#define COMPORT_CONTROL_RTS_REQUEST 10  /* request RTS state */
#define COMPORT_CONTROL_RTS_ON      11
#define COMPORT_CONTROL_RTS_OFF     12
#define COMPORT_CONTROL_FCI_REQUEST 13  /* request inbound FC state */
#define COMPORT_CONTROL_FCI_NONE    14
#define COMPORT_CONTROL_FCI_XONOFF  15
#define COMPORT_CONTROL_FCI_HARDWARE 16
#define COMPORT_CONTROL_FCI_DTR     18

#define COMPORT_LINE_TMOUT          128
#define COMPORT_LINE_TSRE           64
#define COMPORT_LINE_THRE           32
#define COMPORT_LINE_BI             16
#define COMPORT_LINE_FE             8
#define COMPORT_LINE_PE             4
#define COMPORT_LINE_OE             2
#define COMPORT_LINE_DR             1

#define COMPORT_MODEM_CD            128
#define COMPORT_MODEM_RI            64
#define COMPORT_MODEM_DSR           32
#define COMPORT_MODEM_CTS           16
#define COMPORT_MODEM_DCD           8
#define COMPORT_MODEM_TERI          4
#define COMPORT_MODEM_DDSR          2
#define COMPORT_MODEM_DCTS          1

#define COMPORT_PURGE_RX            1
#define COMPORT_PURGE_TX            2
#define COMPORT_PURGE_RXTX          3

#endif /* for TNCOMPORT_H */

/***************************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
