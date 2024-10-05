if (BOOST_FOUND)
    add_library(Boost::pfr ALIAS Boost::headers)
elseif (WIVRN_BUILD_CLIENT)
    FetchContent_MakeAvailable(boost)
else()
    FetchContent_MakeAvailable(boostpfr)
endif()

if (GIT_DESC)
    configure_file(version.cpp.in version.cpp)
else ()
    add_custom_target(wivrn-version ALL
        COMMAND ${CMAKE_COMMAND}
            -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
            -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
            -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
            -D CMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
        )
endif()

configure_file(wivrn_config.h.in wivrn_config.h)

add_library(wivrn-common STATIC
    wivrn_sockets.cpp
    utils/xdg_base_directory.cpp
    vk/allocation.cpp
    vk/error_category.cpp
    vk/vk_allocator.cpp
    vk/vk_mem_alloc.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

if(ANDROID)
    # The Vulkan headers in Android do not include the C++ headers, download them
    # Minimum version is 1.3.256
    set(VULKAN_VERSION "1.3.268.0")

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz ${CMAKE_BINARY_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/KhronosGroup/Vulkan-Headers/archive/vulkan-sdk-${VULKAN_VERSION}.tar.gz ${CMAKE_BINARY_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz
                EXPECTED_HASH SHA256=94993cbe2b1a604c0d5d9ea37a767e1aba4d771d2bfd4ddceefd66243095164f)
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz DESTINATION ${CMAKE_BINARY_DIR})

    target_include_directories(wivrn-common SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/Vulkan-Headers-vulkan-sdk-${VULKAN_VERSION}/include)
    target_compile_definitions(wivrn-common PUBLIC VMA_VULKAN_VERSION=1001000)
else()
    target_link_libraries(wivrn-common PUBLIC Vulkan::Headers)
endif()

target_link_libraries(wivrn-common PUBLIC Boost::pfr wivrn-external)
target_compile_features(wivrn-common PRIVATE cxx_std_20)
target_compile_definitions(wivrn-common PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

target_include_directories(wivrn-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
