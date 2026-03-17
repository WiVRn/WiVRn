if (NOT GIT_DESC)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --always
		OUTPUT_VARIABLE GIT_DESC
		ERROR_QUIET
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
	string(STRIP "${GIT_DESC}" GIT_DESC)
	message(STATUS "Setting version to ${GIT_DESC} from git")
else()
	message(STATUS "Setting version to ${GIT_DESC} from parameters")
endif()

if (NOT GIT_COMMIT)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
		OUTPUT_VARIABLE GIT_COMMIT
		ERROR_QUIET
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
	string(STRIP "${GIT_COMMIT}" GIT_COMMIT)
endif()

if (NOT GIT_DESC)
	message(FATAL_ERROR "GIT_DESC cannot be inferred from .git and was not provided at build time")
endif()

if (GIT_COMMIT)
	message(STATUS "Setting commit to ${GIT_COMMIT}")
else()
	message(FATAL_ERROR "GIT_COMMIT cannot be inferred from .git and was not provided at build time")
endif()

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
