
execute_process(COMMAND git describe --always OUTPUT_VARIABLE GIT_VERSION)
string(STRIP ${GIT_VERSION} GIT_VERSION)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/version.cpp
    COMMAND ${CMAKE_COMMAND}
        -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
        -D INPUT_FILE=${CMAKE_SOURCE_DIR}/common/version.cpp.in
        -D OUTPUT_FILE=${CMAKE_BINARY_DIR}/version.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
)
file(GLOB LOCAL_SOURCE CONFIGURE_DEPENDS "*.cpp")

add_library(wivrn-common STATIC ${CMAKE_BINARY_DIR}/version.cpp ${LOCAL_SOURCES})

target_include_directories(wivrn-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
