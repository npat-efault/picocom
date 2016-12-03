/* vi: set sw=4 ts=4:
 *
 * split.h
 *
 * Function that splits a string intro arguments with quoting.
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

#ifndef SPLIT_H
#define SPLIT_H

/* Maximum single-argument length that can be dealt-with by function
 * split_quoted(). Longer arguments are truncated. See below.
 */
#define MAX_ARG_LEN 512

/* Warning flags, set by split_quoted() to its return value.  */
#define SPLIT_DROP  (1 << 0)  /* argument had to be dropped */
#define SPLIT_TRUNC (1 << 1)  /* argument had to be truncated */


/* F split_quoted
 *
 * Splits string "s" into arguments and places them in "argv". Every
 * argument is a heap-allocated null-terminated string that must be
 * freed with free(3) when no longer needed. The first argument is
 * placed in "argv[*argc]", the following at subsequent "argv" slots,
 * and "*argc" is incremented accordingly. As a result, this function
 * can be called multiple times to add arguments to the same argument
 * vector. The argument "argv_sz" is the allocated size (in number of
 * slots) of the supplied argument vector ("argv"). The function takes
 * care not to overrun it. If more arguments are present in the
 * input string "s", they are dropped.
 *
 * When spliting the input string intro arguments, quoting rules
 * very similar to the ones used by the Unix shell are used.
 *
 * The following caracters are considered special: ' ' (space), '\t'
 * (tab), '\n' (newline), '\' (backslash), ''' (single quote), and '"'
 * (double quote). All other caracters are considered normal and can
 * become part of an argument without escaping.
 *
 * Arguments are separated by runs of the characters: ' ' (space),
 * '\t', and '\n', which are considered delimiters.
 *
 * All characters beetween single quotes (')---without
 * exceptions---are considered normal and become part of the current
 * argument (but not the single quotes themselves).
 *
 * All characters between double quotes (") are considered normal and
 * become part of the current argument (but not the double quotes
 * themselves). Exception to this is the backslash character, when
 * followed by one of the characters '"', '\', '$', and '`'. In this
 * case, the backslash is removed, and the next caracter is considered
 * normal and becomes part of the current argument. When the backslash
 * is followed by newline, both the backslash and the newline are
 * removed. In all other cases a backslash, within double quotes, is
 * considered a normal character (and becomes part of the current
 * argument). We treat the sequences '\$' and '\`' specially (while
 * there is no real reason), for better unix-shell compatibility.
 *
 * Outside of single or double quotes, every backslash caracter is
 * removed, and the following character (with the exception of
 * <newline>, see below) is considered normal and becomes part of the
 * current argument. If, outside of quotes, a backslash precedes a
 * <newline>, then both the backslash and the newline are removed.
 *
 * Examples:
 *
 *      a b c d        --> [a] [b] [c] [d]
 *      'a  b' c   d   --> [a b] [c] [d]
 *      'a "b"' c d    --> [a "b"] [c] [d]
 *      "a 'b'" c d    --> [a 'b'] [c] [d]
 *      a"b c"  d      --> [ab c] [d]
 *      a\ b c d       --> [a b] [c] [d]
 *      \a\b c d       --> [ab] [c] [d]
 *      \a\\b \\ c d   --> [a\b] [\] [c] [d]
 *      "a\$\b" c d    --> [a$\b] [c] [d]
 *      "\a\`\"\b" c d --> [\a`"\b] [c] [d]
 *
 * Limitation: This function cannot deal with individual arguments
 * longer than MAX_ARG_LEN. If such an argument is encountered, it is
 * truncated accordingly.
 *
 * This function returns a non-negative on success, and a negative on
 * failure. The only causes for failure is a malformed command string
 * (e.g. un-balanced quotes), or the inability to allocate an argument
 * string. On success the value returned can be checked against the
 * warning flags SPLIT_DROP, and SPLIT_TRUNC. If SPLIT_DROP is set,
 * then a least one argument was dropped as there was no available
 * slot in "argv" to store it in. If SPLIT_TRUNC is set, then at least
 * one argument was truncated (see limitation, above).
 */
int split_quoted(const char *s, int *argc, char *argv[], int argv_sz);

#endif /* of SPLIT_H */

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
