
FetchContent_MakeAvailable(boostpfr)

if (GIT_DESC)
    configure_file(version.cpp.in version.cpp)
else ()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        COMMAND ${CMAKE_COMMAND}
            -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
            -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
            -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
            -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
    )
endif()
file(GLOB LOCAL_SOURCE CONFIGURE_DEPENDS "*.cpp")

configure_file(wivrn_config.h.in wivrn_config.h)

add_library(wivrn-common STATIC
    wivrn_sockets.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

target_link_libraries(wivrn-common PUBLIC Boost::pfr)
target_compile_features(wivrn-common PRIVATE cxx_std_20)

target_include_directories(wivrn-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
