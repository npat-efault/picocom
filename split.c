/* vi: set sw=4 ts=4:
 *
 * split.c
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "split.h"

/* Lexer error end-codes */
enum err_codes {
    ERR_OK = 0,         /* no error, string lexed ok */
    ERR_BS_AT_EOS,      /* backslash at the end of string */
    ERR_SQ_OPEN_AT_EOS, /* single-quote left open */
    ERR_DQ_OPEN_AT_EOS  /* double-quote left open */
};

/* Lexer states */
enum states {
    ST_DELIM,
    ST_QUOTE,
    ST_ARG,
    ST_END
};

/* Special characters */
#define BS '\\'
#define SQ '\''
#define DQ '\"'
#define NL '\n'
#define EOS '\0'

#define is_delim(c) \
    ( (c) == ' ' || (c) == '\t' || (c) == '\n' )

#define is_dq_escapable(c) \
    ( (c) == '\\' || (c) == '\"' || (c) == '`' || (c) == '$' )

/* Short-hands used in split_quoted() */
#define push()                                  \
    do {                                        \
        char *arg;                              \
        if ( *argc < argv_sz ) {                \
            *ap = '\0';                         \
            arg = strdup(arg_buff);             \
            /* !! out of mem !! */              \
            if ( ! arg ) return -1;             \
            argv[*argc] = arg;                  \
            (*argc)++;                          \
        } else {                                \
            flags |= SPLIT_DROP;                \
        }                                       \
        ap = &arg_buff[0];                      \
    } while(0)

#define save()                                  \
    do {                                        \
        if (ap != ae) {                         \
            *ap++ = *c;                         \
        } else {                                \
            flags |= SPLIT_TRUNC;               \
        }                                       \
    } while (0)

int
split_quoted (const char *s, int *argc, char *argv[], int argv_sz)
{
    char arg_buff[MAX_ARG_LEN]; /* current argument buffer */
    char *ap, *ae;              /* arg_buff current ptr & end-guard */
    const char *c;              /* current input charcter ptr */
    char qc;                    /* current quote character */
    enum states state;          /* current state */
    enum err_codes err;         /* error end-code */
    int flags;                  /* warning flags */

    ap = &arg_buff[0];
    ae = &arg_buff[MAX_ARG_LEN - 1];
    c = &s[0];
    state = ST_DELIM;
    err = ERR_OK;
    flags = 0;
    qc = SQ; /* silence compiler waring */

    while ( state != ST_END ) {
        switch (state) {
        case ST_DELIM:
            while ( is_delim(*c) ) c++;
            if ( *c == SQ || *c == DQ ) {
                qc = *c; c++; state = ST_QUOTE;
                break;
            }
            if ( *c == EOS ) {
                state = ST_END;
                break;
            }
            if ( *c == BS ) {
                c++;
                if ( *c == NL ) {
                    c++;
                    break;
                }
                if ( *c == EOS ) {
                    state = ST_END; err = ERR_BS_AT_EOS;
                    break;
                }
            }
            /* All other cases incl. character after BS */
            save(); c++; state = ST_ARG;
            break;
        case ST_QUOTE:
            while ( *c != qc && ( *c != BS || qc == SQ ) && *c != EOS ) {
                save(); c++;
            }
            if ( *c == qc ) {
                c++; state = ST_ARG;
                break;
            }
            if ( *c == BS ) {
                assert (qc == DQ);
                c++;
                if ( *c == NL) {
                    c++;
                    break;
                }
                if (*c == EOS) {
                    state = ST_END; err = ERR_BS_AT_EOS;
                    break;
                }
                if ( ! is_dq_escapable(*c) ) {
                    c--; save(); c++;
                }
                save(); c++;
                break;
            }
            if ( *c == EOS ) {
                state = ST_END; err = ERR_SQ_OPEN_AT_EOS;
                break;
            }
            assert(0);
        case ST_ARG:
            if ( *c == SQ || *c == DQ ) {
                qc = *c; c++; state = ST_QUOTE;
                break;
            }
            if ( is_delim(*c) || *c == EOS ) {
                push();
                state = (*c == EOS) ? ST_END : ST_DELIM;
                c++;
                break;
            }
            if ( *c == BS ) {
                c++;
                if ( *c == NL ) {
                    c++;
                    break;
                }
                if ( *c == EOS ) {
                    state = ST_END; err = ERR_BS_AT_EOS;
                    break;
                }
            }
            /* All other cases, incl. character after BS */
            save(); c++;
            break;
        default:
            assert(0);
        }
    }

    return ( err != ERR_OK ) ? -1 : flags;
}

/**********************************************************************/

#if 0

int
main (int argc, char *argv[])
{
    char *my_argv[12];
    int my_argc, i, r;

    if ( argc != 2 ) {
        printf("Usage is: %s: <string to split>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("String to split is: [%s]\n", argv[1]);
    r = split_quoted(argv[1], &my_argc, my_argv, 12);
    if ( r < 0 ) {
        printf("Spliting failed!\n");
        exit(EXIT_FAILURE);
    }
    printf("Split ok. SPLIT_DROP is %s, SPLIT_TRUNC is %s\n",
           (r & SPLIT_DROP) ? "ON" : "off",
           (r & SPLIT_TRUNC) ? "ON" : "off");

    for (i = 0; i < my_argc; i++)
        printf("%02d : [%s]\n", i, my_argv[i]);

    return EXIT_SUCCESS;
}

#endif

/**********************************************************************/

/*
 * Local Variables:
 * mode:c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
