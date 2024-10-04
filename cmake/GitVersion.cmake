
execute_process(
	COMMAND ${GIT_EXECUTABLE} describe --tags --always
	OUTPUT_VARIABLE GIT_DESC
	ERROR_QUIET
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

execute_process(
	COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
	OUTPUT_VARIABLE GIT_COMMIT
	ERROR_QUIET
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if (GIT_DESC)
	string(STRIP ${GIT_DESC} GIT_DESC)
	message(STATUS "Setting version to ${GIT_DESC} from git")
else()
	set(GIT_DESC v${CMAKE_PROJECT_VERSION})
	message(STATUS "Setting version to ${GIT_DESC} from CMakeLists")
endif()

if (GIT_COMMIT)
	string(STRIP ${GIT_COMMIT} GIT_COMMIT)
	message(STATUS "Setting commit to ${GIT_COMMIT}")
else()
	set(GIT_COMMIT "")
	message(STATUS "Not setting commit")
endif()

configure_file(${INPUT_FILE} ${OUTPUT_FILE})
