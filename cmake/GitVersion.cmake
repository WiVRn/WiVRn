
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always OUTPUT_VARIABLE GIT_DESC ERROR_QUIET)

if (GIT_DESC)
	string(STRIP ${GIT_DESC} GIT_DESC)
else()
	set(GIT_DESC v${CMAKE_PROJECT_VERSION})
endif()

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
