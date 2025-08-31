if (NOT TARGET OpenSSL::Crypto)
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

    add_library(OpenSSL::SSL STATIC IMPORTED)
    set_property(TARGET OpenSSL::SSL PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/openssl/lib/libssl.so)
    target_include_directories(OpenSSL::SSL INTERFACE ${CMAKE_BINARY_DIR}/openssl/include)
endif()
