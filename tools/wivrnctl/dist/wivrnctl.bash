# bash completion for wivrnctl

_wivrnctl_headsets() {
    wivrnctl list-paired 2>/dev/null \
        | tail -n +2 \
        | awk '{print $1}'
}

_wivrnctl_tabs() {
    cat <<EOF
hidden
overlay_only
compact
stats
settings
foveation_settings
applications
application_launcher
EOF
}

_wivrnctl()
{
    local cur prev cmd
    COMPREPLY=()

    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # Global options
    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W \
            "--help pair unpair rename list-paired stop-server disconnect tab" \
            -- "$cur") )
        return
    fi

    cmd="${COMP_WORDS[1]}"

    case "$cmd" in
        pair)
            case "$prev" in
                -d|--duration)
                    COMPREPLY=( $(compgen -W \
                        "unlimited $(seq 1 10)" \
                        -- "$cur") )
                    return
                    ;;
            esac

            COMPREPLY=( $(compgen -W \
                "-h --help -d --duration" \
                -- "$cur") )
            ;;

        unpair)
            case "$prev" in
                unpair|-h|--help)
                    COMPREPLY=( $(compgen -W "$(_wivrnctl_headsets)" -- "$cur") )
                    return
                    ;;
            esac

            if [[ "$cur" == -* ]]; then
                COMPREPLY=( $(compgen -W "-h --help" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_wivrnctl_headsets)" -- "$cur") )
            fi
            ;;

        rename)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "$(_wivrnctl_headsets)" -- "$cur") )
            elif [[ "$cur" == -* ]]; then
                COMPREPLY=( $(compgen -W "-h --help" -- "$cur") )
            fi
            ;;

        list-paired)
            COMPREPLY=( $(compgen -W \
                "-h --help -k --keys" \
                -- "$cur") )
            ;;

        stop-server|disconnect)
            COMPREPLY=( $(compgen -W \
                "-h --help" \
                -- "$cur") )
            ;;

        tab)
            if [[ "$cur" == -* ]]; then
                COMPREPLY=( $(compgen -W "-h --help" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(_wivrnctl_tabs)" -- "$cur") )
            fi
            ;;

        *)
            ;;
    esac
}

complete -F _wivrnctl wivrnctl
