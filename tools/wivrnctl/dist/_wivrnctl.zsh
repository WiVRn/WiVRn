#compdef wivrnctl


#
# _wivrnctl() {
#   local context state line
#

#     unpair)
#       _arguments \
#         '2:headset:_wivrn_headsets'
#       ;;
#
#     rename)
#       _arguments \
#         '2:headset:_wivrn_headsets' \
#         '3:new headset name:'
#       ;;
#
#     list-paired)
#       local args; args=(
#           "-k:Show public keys"
#           "--keys:Show public keys"
#         )
#       _describe -t args 'arg' args
#       ;;
#
#     tab)
#       _arguments \
#         '2:tab:_wivrn_tabs'
#       ;;
#

(( $+functions[_wivrnctl_pair] )) ||
_wivrnctl_pair(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '(-d --duration=)'{-d,--duration}'[Duration in minutes to allow new connections or \"unlimited\" (default: 2)]:duration:compadd -o nosort unlimited $(seq 0 10)'
}


(( $+functions[_wivrnctl_unpair] )) ||
_wivrnctl_unpair(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '*::headset:_wivrn_headsets'
}


(( $+functions[_wivrnctl_rename] )) ||
_wivrnctl_rename(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '1::headset:_wivrn_headsets' \
        '2::name'
}


(( $+functions[_wivrnctl_list-paired] )) ||
_wivrnctl_list-paired(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '(-)'{-k,--keys}'[Show public keys]'
}

(( $+functions[_wivrnctl_stop-server] )) ||
_wivrnctl_stop-server(){
    _arguments \
        '(-)'{-h,--help}'[Print help]'
}

(( $+functions[_wivrnctl_disconnect] )) ||
_wivrnctl_disconnect(){
    _arguments \
        '(-)'{-h,--help}'[Print help]'
}

(( $+functions[_wivrnctl_tab] )) ||
_wivrnctl_tab(){
    _arguments \
        '(-)'{-h,--help}'[Print help]' \
        '*::tab:_wivrn_tabs'
}

(( $+functions[_wivrnctl_tabs] )) ||
_wivrn_tabs() {
  local -a tabs

  tabs=(
    "hidden"
    "overlay_only"
    "compact"
    "stats"
    "settings"
    "foveation_settings"
    "applications"
    "application_launcher"
  )
  _describe -t tabs 'tab' tabs
}

(( $+functions[_wivrnctl_headsets] )) ||
_wivrn_headsets() {
  local -a headsets

  headsets=("${(@f)$(
      wivrnctl list-paired 2>/dev/null | tail -n +2 | awk '{print $1 ":" substr($0, index($0,$2))}'
  )}")

  _describe -t headsets 'headset' headsets
}

(( $+functions[_wivrnctl_commands] )) ||
_wivrnctl_commands(){
    local -a _wivrnctl_cmds
    _wivrnctl_cmds=(
         'pair:Allow a new headset to connect'
         'unpair:Remove a headset'
         'rename:Rename a headset'
         'list-paired:List headsets allowed to connect'
         'stop-server:Stop wivrn-server process'
         'disconnect:Disconnect headset'
         'tab:Show or set current tab on headset'
    )

    if ((CURRENT == 1)); then
        _describe -t commands 'wivrnctl commands' _wivrnctl_cmds
    else
        local curcontext="$curcontext"
        cmd="${${_wivrnctl_cmds[(r)$words[1]:*]%%:*}}"
        if (($#cmd)); then
            if (( $+functions[_wivrnctl_$cmd] )); then
                _wivrnctl_$cmd
            else
                _message "no options for $cmd"
            fi
        else
            _message "no more options"
        fi
    fi
}

_arguments \
    '(- *)--help[Print help text]' \
    '*::wivrnctl commands:_wivrnctl_commands'
