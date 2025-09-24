if (WIVRN_USE_SYSTEM_BOOST)
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

add_library(wivrn-common-base STATIC EXCLUDE_FROM_ALL
    utils/strings.cpp
    utils/xdg_base_directory.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

add_library(wivrn-common STATIC EXCLUDE_FROM_ALL
    crypto.cpp
    smp.cpp
    secrets.cpp
    wivrn_sockets.cpp
    utils/strings.cpp
    vk/allocation.cpp
    vk/error_category.cpp
    vk/vk_allocator.cpp
    vk/vk_mem_alloc.cpp
)

if (WIVRN_BUILD_DASHBOARD OR WIVRN_BUILD_SERVER)
    add_library(wivrn-common-server STATIC EXCLUDE_FROM_ALL
        utils/flatpak.cpp
        utils/xdg_icon_lookup.cpp
        utils/steam_app_info.cpp
        utils/load_icon.cpp
        utils/ini.cpp
        application.cpp
    )
    target_link_libraries(wivrn-common-server PRIVATE nlohmann_json::nlohmann_json PNG::PNG PkgConfig::librsvg-2.0 LibArchive::LibArchive PUBLIC wivrn-common-base)

    if (WIVRN_BUILD_TEST)
        add_executable(list-apps
            test_application.cpp
        )

        target_link_libraries(list-apps wivrn-common-server)
    endif()
endif()

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


    # The NDK does not include OpenSSL, download it
    set(OPENSSL_VERSION "3.4.0")
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/openssl-src)
        if(NOT EXISTS ${CMAKE_BINARY_DIR}/openssl-${OPENSSL_VERSION}.tar.gz)
            if (EXISTS ${CMAKE_SOURCE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz)
                file(CREATE_LINK ${CMAKE_SOURCE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz ${CMAKE_BINARY_DIR}/openssl-${OPENSSL_VERSION}.tar.gz SYMBOLIC)
            else()
                file(DOWNLOAD https://github.com/openssl/openssl/archive/refs/tags/openssl-${OPENSSL_VERSION}.tar.gz ${CMAKE_BINARY_DIR}/openssl-${OPENSSL_VERSION}.tar.gz
                    EXPECTED_HASH SHA256=1ca043a26fbea74cdf7faf623a6f14032a01117d141c4a5208ccac819ccc896b)
            endif()
        endif()

        file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/openssl-${OPENSSL_VERSION}.tar.gz DESTINATION ${CMAKE_BINARY_DIR})
        file(RENAME ${CMAKE_BINARY_DIR}/openssl-openssl-${OPENSSL_VERSION} ${CMAKE_BINARY_DIR}/openssl-src)
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} ./Configure
            android-arm64
            --prefix=${CMAKE_BINARY_DIR}/openssl
            --openssldir=${CMAKE_BINARY_DIR}/openssl
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/openssl-src
        OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-config-out
        ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-config-err
    )

    if (NOT EXISTS ${CMAKE_BINARY_DIR}/openssl/lib/libcrypto.so)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} make -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/openssl-src
            OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-make-out
            ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-make-err
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} make install_sw
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/openssl-src
            OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-install-out
            ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-install-err
        )
    endif()

    add_library(OpenSSL::Crypto STATIC IMPORTED)
    set_property(TARGET OpenSSL::Crypto PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/openssl/lib/libcrypto.so)
    target_include_directories(OpenSSL::Crypto INTERFACE ${CMAKE_BINARY_DIR}/openssl/include)
else()
    target_link_libraries(wivrn-common PUBLIC Vulkan::Headers)
endif()

target_include_directories(wivrn-common-base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wivrn-common PUBLIC Boost::pfr OpenSSL::Crypto wivrn-external wivrn-common-base)
target_compile_definitions(wivrn-common PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)
