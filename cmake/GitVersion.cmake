
execute_process(COMMAND git describe --always OUTPUT_VARIABLE GIT_VERSION)
string(STRIP ${GIT_VERSION} GIT_VERSION)

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
