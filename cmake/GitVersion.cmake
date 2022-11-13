
execute_process(COMMAND git describe --always OUTPUT_VARIABLE GIT_DESC)
string(STRIP ${GIT_DESC} GIT_DESC)

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
