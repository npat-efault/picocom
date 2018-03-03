# Custom bash completion for picocom.
#
# Source this file like this:
#     . <picocom-src-dir>/bash-completion/picocom
# Or arrange for it to be sourced by your ".bashrc",
# Or copy it in /etc/bash_completion.d (it will be sourced automatically)
#
# Provides simple custom completions for option names and option
# values, while keeping the standard ones (variable, pathname, etc) if
# the custom ones don't produce matches. This script does *not* depend
# on the "bash-completion" package (just plain Bash) and it does not
# use any of its helper functions. The code is not bullet-proof; you
# *can* confuse it with strange input if you try.
#
# Tested with:
#   GNU bash, version 4.3.48(1)-release (x86_64-pc-linux-gnu)
#   GNU bash, version 4.4.18(1)-release (x86_64-unknown-linux-gnu)
#
# by Nick Patavalis (npat@efault.net)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA
#

# _picocom_split_line
#
# Splits line into words. Splits line in almost exatcly the same way
# as readline does it. Using this function, you can specify your own
# delimiter and separator characters without having to change
# COMP_WORDBREAKS (which may affect other completion functions and
# scripts). In addtion, this function takes into account COMP_POINT
# and splits up to it, which may help when completing to the middle of
# an argument. The line is split taking into account quoting
# (backslash, single and double), as well as ${...}, $(...), and
# backquoting.  Words are broken-up either by unquoted delimiter
# characters (which do not end-up in the resulting word-stream) or by
# unquoted separator characters, which do. Consequitive separator
# characters are treated as single word.
#
# The line to be split is read from ${COMP_LINE:0:$COMP_POINT}.
# Delimiters are specified by the "-d" option, and separators by
# "-s". If missing, default delimiters and separators are
# used. Results are left in the "words" array.
#
function _picocom_split_line()
{
    local delimiters=$' \t\n'
    local separators=$'=><'
    local flag line wbreaks state word c c1 i
    local -a stack

    while getopts "d:s:" flag "$@"; do
        case $flag in
            d) delimiters=$OPTARG ;;
            s) separators=$OPTARG ;;
        esac
    done

    # state names: ' (single quote),
    #              " (double quote),
    #              { (string-brace),
    #              ( (string-paren)
    #              ` (backquote)
    #              D (delimiter)
    #              W (naked word)
    #    
    # ", ${, and $( can nest inside each-other any number of times

    wbreaks=$delimiters$separators
    line=${COMP_LINE:0:$COMP_POINT}
    state=D
    for (( i=0; i<${#line}; i++)); do
        c=${line:i:1}
        c1=${line:i+1:1}
        #echo "  [$i\t$c\t$c1\t$state\t$word\t\t${stack[*]} ]" >> $DEBUG
        if [[ $state == D || $state == W ]]; then
            if [[ $c == [$wbreaks] ]]; then
                if [[ $state == W ]]; then
                    words+=( "$word" )
                    word=
                    state=D
                fi
                # handle separators
                if [[ $c == [$separators] ]]; then
                    while [[ $c == [$separators] ]]; do
                        word+=$c
                        let i++
                        c=${line:i:1}
                    done
                    if [[ -n $c ]]; then
                        # emit word (but not at eol)
                        words+=( "$word" )
                        word=
                    fi
                    let i--
                fi
                continue
            elif [[ $c == [\'\"\`] ]]; then
                stack+=( W )
                state=$c
            elif [[ $c == '\' ]]; then
                word+=$c
                let i++
                c=$c1
                state=W
            elif [[ $c == '$' && ( $c1 == '(' || $c1 == '{' )  ]]; then
                word+=$c
                let i++
                c=$c1
                stack+=( W )
                state=$c1
            else
                state=W
            fi
            word+=$c
        elif [[ $state == "'" ]]; then
            if [[ $c == "'" ]]; then
                state=${stack[-1]}
                unset stack[-1]
            fi
            word+=$c
        elif [[ $state == '`' ]]; then
            if [[ $c == '\' ]]; then
                word+=$c
                let i++
                c=$c1
            elif [[ $c == '`' ]]; then
                state=${stack[-1]}
                unset stack[-1]
            fi
            word+=$c
        elif [[ $state == '"' ]]; then
            if [[ $c == '\' ]]; then
                word+=$c
                let i++
                c=$c1
            elif [[ $c == '`' ]]; then
                stack+=( W )
                state=$c                
            elif [[ $c == '$' && ( $c1 == '(' || $c1 == '{' ) ]]; then
                let i++
                word+=$c
                c=$c1
                stack+=( $state )
                state=$c1
            elif [[ $c == '"' ]]; then
                state=${stack[-1]}
                unset stack[-1]
            fi
            word+=$c
        elif [[ $state == '(' || $state == '{' ]]; then
            if [[ $c == [\'\"\`] ]]; then
                stack+=( $state )
                state=$c
            elif [[ $c == '\' ]]; then
                word+=$c
                let i++
                c=$c1
            elif [[ $c == '$' && ( $c1 == '(' || $c1 == '{' ) ]]; then
                let i++
                word+=$c
                c=$c1
                stack+=( $state )
                state=$c
            elif [[ $state$c == '{}' || $state$c == "()" ]]; then
                state=${stack[-1]}
                unset stack[-1]
            fi
            word+=$c
        fi
    done
    words+=( "$word" )
}

_picocom_split_line_debug()
{
    echo "------------" >> $DEBUG
    echo "[ $COMP_POINT ] $COMP_LINE" >> $DEBUG
    local wrd
    for wrd in "${words[@]}"; do
        echo -n "$wrd | " >> $DEBUG
    done
    echo >> $DEBUG
}

# Remove quoting from $1. Fails if $1 includes (unescaped) characters
# that can cause expansions.
_picocom_dequote()
{
    local quoted="$1"
    local i c c1 inside word

    for (( i=0; i<${#quoted}; i++ )); do
        c=${quoted:i:1}
        c1=${quoted:i+1:1}
        #echo "  [$c] [$c1] [$inside]" >> $DEBUG
        if [[ -z $inside ]]; then
            if [[ $c == '\' ]]; then
                let i++
                c=$c1
                if [[ $c = $'\n' ]]; then
                    let i++
                    c=${quoted:i:1}
                fi
            elif [[ $c == [\'\"] ]]; then
                inside=$c
                continue
            elif [[ $c == [\$\`\[\!*] ]]; then
                return 1
            fi
        elif [[ $inside == "'" ]]; then
            if [[ $c == "'" ]]; then
                inside=
                continue
            fi
        elif [[ $inside == '"' ]]; then
            if [[ $c == '\' && $c1 == [\\\"\$\`\!$'\n'] ]]; then
                [[ $c1 == '!' ]] && word+=$c
                let i++
                c=$c1
            elif [[ $c == '"' ]]; then
                inside=
                continue
            elif [[ $c == [\$\`\!] ]]; then
                return 1
            fi
        fi
        word+=$c
    done
    echo "$word"
    return 0
}

# Remove character mappings that already appear in $cur. Results are
# left in array "mapfilt"
_picocom_filter_mappings()
{
    local IFS cur1 m c found
    local -a cura

    cur1=$(_picocom_dequote "$cur") || return 1
    IFS=$', \t'
    cura=( $cur1 )
    IFS=$' \t\n'
    # consider last mapping partial unless string ends in separator
    [[ ${#cura[@]} -gt 0 && $cur1 != *[$', \t'] ]] && unset cura[-1]
    for m in "${mappings[@]}"; do
        found=
        for c in "${cura[@]}"; do
            [[ $c == "$m" ]] && { found=yes; break; }
        done
        [[ -z $found ]] && mapfilt+=( "$m" )
    done
    return 0
}

# Check if $1 is valid picocom option name
_picocom_is_opt()
{
    local e match="$1"
    for e in "${opts[@]}"; do
        [[ $e == "$match" ]] && return 0
    done
    return 1
}

# Custom completion function for picocom
_picocom()
{
    local cur cur0 cur1 prev
    local -a words opts baudrates mappings mapfilt
    local DEBUG=/dev/null
    
    opts=( --baud --flow --databits --stopbits --parity \
           --lower-rts --lower-dtr --raise-rts --raise-dtr \
           --imap --omap --emap
           --echo --initstring \
           --noinit --noreset --hangup \
           --receive-cmd --send-cmd \
           --escape --no-escape \
           --logfile \
           --exit-after --exit \
           --nolock \
           --quiet --help )

    baudrates=( 50 75 110 134 150 200 300 600 1200 1800 2400 4800 9600 \
               19200 38400 57600 115200 \
               230400 460800 500000 576000 921600 1000000 1152000 1500000 \
               2000000 2500000 3000000 3500000 4000000 )

    mappings=( crlf crcrlf igncr lfcr lfcrlf ignlf delbs bsdel \
              spchex tabhex crhex lfhex 8bithex nrmhex )

    # We split the line ourselves. We don't use COMP_WORDS
    _picocom_split_line
    cur="${words[-1]}"
    prev="${words[-2]}"

    # Handle option values given with "="
    if [[ $cur =~ "="+ ]]; then
        _picocom_is_opt "$prev" && cur=
    fi
    if [[ $prev =~ "="+ ]]; then
        _picocom_is_opt "${words[-3]}" && prev="${words[-3]}"
    fi

    # Complete option values
    case "$prev" in
        -b | --baud)
            COMPREPLY=( $(compgen -W "${baudrates[*]}" -- "$cur") )
            if [[ ${#COMPREPLY[@]} -ne 0 ]]; then
               # This only works for bash 4.4 and newer
               compopt -o nosort > /dev/null 2>&1
            fi
            return 0
            ;;
        -f | --flow)
            COMPREPLY=( $(compgen -W "hard soft none" -- "$cur") )
            return 0
            ;;
        -d | --databits)
            COMPREPLY=( $(compgen -W "5 6 7 8" -- "$cur") )
            return 0
            ;;
        -p | --stopbits)
            COMPREPLY=( $(compgen -W "1 2" -- "$cur") )
            return 0
            ;;
        -y | --parity)
            COMPREPLY=( $(compgen -W "even odd none" -- "$cur") )
            return 0
            ;;        
        -I | --imap | -O | --omap | -E | --emap )
            _picocom_filter_mappings || return 0
            cur1=${cur##*[, $'\t']}
            cur0=${cur%"$cur1"}
            #echo "$cur0 | $cur1 |" >> $DEBUG
            local IFS=$'\n'
            COMPREPLY=( $(compgen -P "$cur0" -S ","  -W "${mapfilt[*]}" -- "$cur1") )
            #echo "${COMPREPLY[*]}" >> $DEBUG
            if [[ ${#COMPREPLY[@]} -ne 0 ]]; then
               compopt -o nospace
               # This only works for bash-4.4 and newer
               compopt -o nosort > /dev/null 2>&1
            fi
            return 0
            ;;
        -v | --receive-cmd | -s | --send-cmd)
            # Do default completion
            return 0
            ;;
        -e | --escape)
            # Do default completion
            return 0
            ;;
        -g | --logfile)
            # Do default completion
            return 0
            ;;
        -t | --initstring)
            # Do default completion
            return 0
            ;;
        -x | --exit-after)
            # Do default completion
            return 0
            ;;
        *)
            ;;
    esac

    # Complete option names
    if [[ $cur == -* ]] ; then
        COMPREPLY=( $(compgen -W "${opts[*]}" -- "$cur") )
        # This only works for bash 4.4 and newer
        compopt -o nosort > /dev/null 2>&1
        return 0
    fi

    if [[ -z $cur ]]; then
        COMPREPLY=( $(compgen -G "/dev/tty*") )
        return 0
    fi
    return 0
}

# Bind custom completion function to command
complete -o default -o bashdefault -F _picocom picocom

# Local variables:
# mode: sh
# End:
