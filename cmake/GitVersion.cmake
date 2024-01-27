
execute_process(
	COMMAND ${GIT_EXECUTABLE} describe --tags --always
	OUTPUT_VARIABLE GIT_DESC
	ERROR_QUIET
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if (GIT_DESC)
	string(STRIP ${GIT_DESC} GIT_DESC)
	message(STATUS "Setting version to ${GIT_DESC} from git")
else()
	set(GIT_DESC v${CMAKE_PROJECT_VERSION})
	message(STATUS "Setting version to ${GIT_DESC} from CMakeLists")
endif()

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
