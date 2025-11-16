if (NOT TARGET OpenSSL::Crypto)
    # The NDK does not include OpenSSL, download it
    set(OPENSSL_VERSION "3.6.0")
    set(OPENSSL_SHA256 b6a5f44b7eb69e3fa35dbf15524405b44837a481d43d81daddde3ff21fcbb8e9)
    if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/openssl-src)
        if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz)
            if (EXISTS ${CMAKE_SOURCE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz)
                file(CREATE_LINK ${CMAKE_SOURCE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz ${FETCHCONTENT_BASE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz SYMBOLIC)
            else()
                file(DOWNLOAD https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz
                     ${FETCHCONTENT_BASE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz
                     EXPECTED_HASH SHA256=${OPENSSL_SHA256})
            endif()
        endif()

        file(ARCHIVE_EXTRACT INPUT ${FETCHCONTENT_BASE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz DESTINATION ${FETCHCONTENT_BASE_DIR})
        file(RENAME ${FETCHCONTENT_BASE_DIR}/openssl-${OPENSSL_VERSION} ${FETCHCONTENT_BASE_DIR}/openssl-src)
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} ./Configure
            android-arm64
            --prefix=${FETCHCONTENT_BASE_DIR}/openssl
            --openssldir=${FETCHCONTENT_BASE_DIR}/openssl
        WORKING_DIRECTORY ${FETCHCONTENT_BASE_DIR}/openssl-src
        OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-config-out
        ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-config-err
    )

    if (NOT EXISTS ${FETCHCONTENT_BASE_DIR}/openssl/lib/libcrypto.so)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} make -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY ${FETCHCONTENT_BASE_DIR}/openssl-src
            OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-make-out
            ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-make-err
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK} PATH=${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin:$ENV{PATH} make install_sw
            WORKING_DIRECTORY ${FETCHCONTENT_BASE_DIR}/openssl-src
            OUTPUT_FILE ${CMAKE_BINARY_DIR}/openssl-install-out
            ERROR_FILE ${CMAKE_BINARY_DIR}/openssl-install-err
        )
    endif()

    add_library(OpenSSL::Crypto STATIC IMPORTED)
    set_property(TARGET OpenSSL::Crypto PROPERTY IMPORTED_LOCATION ${FETCHCONTENT_BASE_DIR}/openssl/lib/libcrypto.so)
    target_include_directories(OpenSSL::Crypto INTERFACE ${FETCHCONTENT_BASE_DIR}/openssl/include)

    add_library(OpenSSL::SSL STATIC IMPORTED)
    set_property(TARGET OpenSSL::SSL PROPERTY IMPORTED_LOCATION ${FETCHCONTENT_BASE_DIR}/openssl/lib/libssl.so)
    target_include_directories(OpenSSL::SSL INTERFACE ${FETCHCONTENT_BASE_DIR}/openssl/include)
endif()
