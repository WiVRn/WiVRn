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
	set(GIT_DESC v${CMAKE_PROJECT_VERSION})
	message(STATUS "Setting version to ${GIT_DESC} from CMakeLists")
endif()

if (GIT_COMMIT)
	message(STATUS "Setting commit to ${GIT_COMMIT}")
else()
	set(GIT_COMMIT "")
	message(STATUS "Not setting commit")
endif()

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
