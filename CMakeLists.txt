cmake_minimum_required(VERSION 3.28)
project(WiVRn VERSION 0.0.0 LANGUAGES C CXX)

# Set default for FetchContent projects
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ANDROID)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/android)
endif()

# Default to PIC code
include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(ExternalProject)
include(GNUInstallDirs)

option(WIVRN_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
option(WIVRN_USE_SYSTEM_BOOST "Use system Boost headers" ON)
option(WIVRN_USE_SYSTEM_OPENXR "Use system OpenXR headers" ON)
option(WIVRN_USE_SYSTEM_LIBKTX "Use system KTX-Software library" ON)
option(WIVRN_USE_SYSTEMD "Use libsystemd" ON)
option(WIVRN_COMPRESS_GLB "Compress textures in GLB files with gltf-transform" OFF)
option(WIVRN_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WIVRN_USE_ANDROID_BIN_LIBKTX "Download the prebuilt libktx for android" OFF)
option(WIVRN_BUILD_CLIENT "Build WiVRn client" OFF)
option(WIVRN_BUILD_SERVER "Build WiVRn server" ON)
option(WIVRN_BUILD_SERVER_LIBRARY "Build OpenXR library for WiVRn server" ${WIVRN_BUILD_SERVER})
option(WIVRN_BUILD_DASHBOARD "Build WiVRn dashboard" OFF)
option(WIVRN_BUILD_WIVRNCTL "Build wivrnctl" ON)
option(WIVRN_BUILD_DISSECTOR "Build Wireshark dissector" OFF)
option(WIVRN_BUILD_TEST "Build test binaries" OFF)
option(WIVRN_WERROR "Treat warnings as errors" OFF)
option(WIVRN_OPTIMIZE_SHADERS "Run siprv-opt on compiled shaders" ON)

option(WIVRN_USE_NVENC "Enable nvenc (Nvidia) hardware encoder" ON)
option(WIVRN_USE_VAAPI "Enable vaapi (AMD/Intel) hardware encoder" ON)
option(WIVRN_USE_VULKAN_ENCODE "Enable vulkan video encoder" ON)
option(WIVRN_USE_X264 "Enable x264 software encoder" ON)

option(WIVRN_USE_PIPEWIRE "Enable pipewire backend" ON)
option(WIVRN_USE_PULSEAUDIO "Enable pulseaudio backend" OFF)

set(OVR_COMPAT_SEARCH_PATH "/opt/xrizer:/usr/local/lib/OpenComposite:/usr/lib/OpenComposite:/opt/OpenComposite:/opt/opencomposite"
    CACHE STRING "List of places to search for the OpenVR compatibility layer, separated by :")

option(WIVRN_FEATURE_DEBUG_GUI "Enable Monado debug GUI" OFF)
option(WIVRN_FEATURE_RENDERDOC "Support renderdoc" OFF)
set(WIVRN_FEATURE_SOLARXR OFF) # Will be added back when merged in upstream Monado
option(WIVRN_FEATURE_STEAMVR_LIGHTHOUSE "Enable SteamVR Lighthouse driver" OFF)

option(WIVRN_CHECK_CAPSYSNICE "Check if CAP_SYS_NICE is available at startup" ON)
set(WIVRN_CSS "${CMAKE_SOURCE_DIR}/images/wivrn-local.css" CACHE STRING "Stylesheet to use to render SVGs" )

option(WIVRN_SHOW_IMGUI_DEMO_WINDOW "Show the Dear ImGui demo window in the client" OFF)
option(WIVRN_CLIENT_DEBUG_MENU "Enable the debug menu in the client" OFF)

set(WIVRN_OPENXR_MANIFEST_TYPE_VALUES relative absolute filename)
set(WIVRN_OPENXR_MANIFEST_TYPE "relative" CACHE STRING "How to reference libraries in OpenXR manifest: ${WIVRN_OPENXR_MANIFEST_TYPE_VALUES}")
if (NOT WIVRN_OPENXR_MANIFEST_TYPE IN_LIST WIVRN_OPENXR_MANIFEST_TYPE_VALUES)
    message(FATAL_ERROR "WIVRN_OPENXR_MANIFEST_TYPE must be one of ${WIVRN_OPENXR_MANIFEST_TYPE_VALUES}")
endif()

option(ENABLE_COLOURED_OUTPUT "Always produce ANSI-coloured output (GNU/Clang only)." ON)

set(WIVRN_ENVIRONMENTS_URL "https://github.com/WiVRn/WiVRn-environments/releases/latest/download/index.json" CACHE STRING "URL hosting the custom environments json file")

if (WIVRN_BUILD_CLIENT OR WIVRN_BUILD_SERVER OR WIVRN_BUILD_DASHBOARD OR WIVRN_BUILD_DISSECTOR)
    set(NEED_COMMON TRUE)
else()
    set(NEED_COMMON FALSE)
endif()

if(ENABLE_COLOURED_OUTPUT)
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options(-fdiagnostics-color=always)
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		add_compile_options(-fcolor-diagnostics)
	endif()
endif()

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Git)
if (NEED_COMMON)
    find_package(OpenSSL REQUIRED)
endif()
include(VulkanVersion)

if (WIVRN_USE_SYSTEM_BOOST AND NEED_COMMON)
    if (WIVRN_BUILD_DISSECTOR)
        find_package(Boost 1.84.0 REQUIRED COMPONENTS locale url)
    else()
        find_package(Boost 1.75.0 REQUIRED COMPONENTS locale url)
    endif()
endif()

if (WIVRN_BUILD_SERVER)
    find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)
    if (Vulkan_VERSION  VERSION_LESS "1.3.261")
        message(FATAL_ERROR "Vulkan version must be at least 1.3.261, found ${Vulkan_VERSION}")
    endif()

    if (NOT WIVRN_USE_NVENC AND NOT WIVRN_USE_VAAPI AND NOT WIVRN_USE_X264 AND NOT WIVRN_USE_VULKAN_ENCODE)
        message(FATAL_ERROR "No encoder selected, use at least one of WIVRN_USE_NVENC, WIVRN_USE_VAAPI, WIVRN_USE_VULKAN_ENCODE or WIVRN_USE_X264")
    endif()

    if (WIVRN_USE_VAAPI)
        pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
        pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    endif()

    if (WIVRN_USE_VULKAN_ENCODE)
        if (Vulkan_VERSION VERSION_LESS 1.3.283)
            message(FATAL_ERROR "Vulkan video encode requires 1.3.283, found ${Vulkan_VERSION}")
        endif()
    endif()

    if (WIVRN_USE_X264)
        pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
    endif()

    if (WIVRN_USE_SYSTEMD)
        pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    endif()

    if (WIVRN_USE_PIPEWIRE)
        pkg_check_modules(libpipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
    endif()

    if (WIVRN_USE_PULSEAUDIO)
        pkg_check_modules(libpulse REQUIRED IMPORTED_TARGET libpulse)
    endif()

    pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client avahi-glib)
    find_package(Eigen3 REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(CLI11 REQUIRED)

    pkg_check_modules(glib2 REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)

    pkg_check_modules(libnotify REQUIRED IMPORTED_TARGET libnotify)
endif()

if (WIVRN_BUILD_DASHBOARD OR WIVRN_BUILD_SERVER)
    pkg_check_modules(librsvg-2.0 REQUIRED IMPORTED_TARGET librsvg-2.0)
    find_package(LibArchive REQUIRED)
    find_package(PNG REQUIRED)
endif()

if (WIVRN_BUILD_DASHBOARD)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick Test Gui QuickControls2 Widgets Network)

    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10.0)
        # QtCoro needs this for Qt 6.10 and (currently) doesn't do this by itself
        find_package(Qt6 REQUIRED COMPONENTS QmlPrivate)
    endif()

    find_package(ECM 5.115 REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

    find_package(KF6 REQUIRED COMPONENTS Kirigami I18n CoreAddons QQC2DesktopStyle IconThemes)
    find_package(KF6Kirigami)
    find_package(nlohmann_json REQUIRED)
    find_package(QCoro6 REQUIRED COMPONENTS Core Network Qml)
    qcoro_enable_coroutines()

    if (WIVRN_CHECK_CAPSYSNICE)
        pkg_check_modules(libcap REQUIRED IMPORTED_TARGET libcap)
    endif()

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

if (WIVRN_BUILD_CLIENT OR WIVRN_BUILD_DASHBOARD)
    find_program(RSVG_CONVERT rsvg-convert REQUIRED)
    find_package(Gettext REQUIRED)
endif()

if (WIVRN_BUILD_CLIENT)
    find_program(TOKTX toktx REQUIRED)

    if (WIVRN_COMPRESS_GLB)
        find_program(GLTF_TRANSFORM gltf-transform REQUIRED)
    endif()

    if(NOT ANDROID)
        find_package(CURL REQUIRED COMPONENTS HTTP HTTPS)
    endif()
endif()

if (WIVRN_BUILD_CLIENT AND NOT ANDROID)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
    find_package(Fontconfig REQUIRED)

    if (WIVRN_USE_SYSTEM_OPENXR)
        find_package(OpenXR 1.1.48 REQUIRED)
    endif()

    if (WIVRN_USE_SYSTEM_LIBKTX)
        find_package(Ktx REQUIRED)
    endif()
endif()

if(ANDROID)
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

if (WIVRN_BUILD_DISSECTOR)
    if (WIVRN_USE_SYSTEM_OPENXR)
        find_package(OpenXR 1.1.48 REQUIRED)
    endif()
    find_package(Wireshark REQUIRED)
endif()

if (WIVRN_BUILD_WIVRNCTL)
    find_package(CLI11 REQUIRED)
    pkg_search_module(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd libelogind)
endif()

# Common dependencies
FetchContent_Declare(boostpfr      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/boostorg/pfr/archive/refs/tags/2.2.0.tar.gz
                                   URL_HASH SHA256=6f490ec1383de5855dcf801c23f0a967f888eb6e1440b6ca0bf36c12d4ae7ac7)
FetchContent_Declare(boost         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/boostorg/boost/releases/download/boost-1.89.0/boost-1.89.0-cmake.tar.xz
                                   URL_HASH SHA256=67acec02d0d118b5de9eb441f5fb707b3a1cdd884be00ca24b9a73c995511f74)

# Client dependencies
FetchContent_Declare(simdjson      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/simdjson/simdjson/archive/refs/tags/v4.2.2.tar.gz
                                   URL_HASH SHA256=3efae22cb41f83299fe0b2e8a187af543d3dda93abbb910586f897df670f9eaa)
FetchContent_Declare(fastgltf      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.9.0.tar.gz
                                   URL_HASH SHA256=0bb564e127b14c22f062db50f89381dd2e0a20dbaf4987ca138a4ae8728712f9)
FetchContent_Declare(glm           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/g-truc/glm/releases/download/1.0.2/glm-1.0.2.zip
                                   URL_HASH SHA256=a41adf423315e35489c1e67689c7bb74c7edd4d207a477b7b19c45cd440c2f67)
FetchContent_Declare(openxr_loader EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.48.tar.gz
                                   URL_HASH SHA256=c9372992dcdfac643e28796e41d4dbd36c4cdf0388d01fa89245628edae9d411)
FetchContent_Declare(spdlog        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.tar.gz
                                   URL_HASH SHA256=8741753e488a78dd0d0024c980e1fb5b5c85888447e309d9cb9d949bdb52aa3e)
FetchContent_Declare(stb           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.zip
                                   URL_HASH SHA256=b7f476902bbef1b30f8ecc2d9d95c459c32302c8b559d09b589b5955463b7af8)
set(KTX_VERSION "4.4.2")
set(KTX_VERSION_FULL "v${KTX_VERSION}")
FetchContent_Declare(libktx        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/${KTX_VERSION_FULL}.tar.gz
                                   URL_HASH SHA256=9412cb45045a503005acd47d98f9e8b47154634a50b4df21e17a1dfa8971d323
                                   )
FetchContent_Declare(implot        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/epezent/implot/archive/3da8bd34299965d3b0ab124df743fe3e076fa222.zip
                                   URL_HASH SHA256=e9b439a94aa6f09fcf12ed9c2cd784efe5e99e96242122aa683e690b29f7a59f)
file(GLOB IMGUI_PATCHES CONFIGURE_DEPENDS patches/imgui/*)
FetchContent_Declare(imgui         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.4.tar.gz
                                   URL_HASH SHA256=0e175d4d941112532549b418ced0bd546abe9024ecb9b5f431f8a67a2197b0ba
                                   PATCH_COMMAND    ${CMAKE_CURRENT_LIST_DIR}/patches/apply.sh ${IMGUI_PATCHES}
)
FetchContent_Declare(uni-algo      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/uni-algo/uni-algo/archive/v1.2.0.tar.gz
                                   URL_HASH SHA256=f2a1539cd8635bc6088d05144a73ecfe7b4d74ee0361fabed6f87f9f19e74ca9)
FetchContent_Declare(entt          EXCLUDE_FROM_ALL SYSTEM URL https://github.com/skypjack/entt/archive/refs/tags/v3.15.0.tar.gz
                                   URL_HASH SHA256=01466fcbf77618a79b62891510c0bbf25ac2804af5751c84982b413852234d66)
FetchContent_Declare(curl          EXCLUDE_FROM_ALL SYSTEM URL https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.xz
                                   URL_HASH SHA256=955f6e729ad6b3566260e8fef68620e76ba3c31acf0a18524416a185acf77992)
FetchContent_Declare(spirv-reflect EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/tags/vulkan-sdk-1.4.328.1.tar.gz
                                   URL_HASH SHA256=77f4b5b5630d960d7c17e39ddd5bf2e7dd62e6d8da24259ed14ca5b59783ea35)

file(GLOB MONADO_PATCHES CONFIGURE_DEPENDS patches/monado/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS monado-rev)
file(STRINGS monado-rev MONADO_REV)
# Server dependencies
FetchContent_Declare(monado
    URL https://gitlab.freedesktop.org/monado/monado/-/archive/${MONADO_REV}/monado-${MONADO_REV}.tar.gz
    PATCH_COMMAND    ${CMAKE_CURRENT_LIST_DIR}/patches/apply.sh ${MONADO_PATCHES}
    EXCLUDE_FROM_ALL
)

add_subdirectory(external)
if (NEED_COMMON)
    add_subdirectory(common)
endif()

if (WIVRN_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WIVRN_BUILD_SERVER OR WIVRN_BUILD_SERVER_LIBRARY)
    add_subdirectory(server)

    if (WIVRN_BUILD_SERVER)
        message("Selected encoders:")
        message("\tNVENC : ${WIVRN_USE_NVENC}")
        message("\tVAAPI : ${WIVRN_USE_VAAPI}")
        message("\tVulkan: ${WIVRN_USE_VULKAN_ENCODE}")
        message("\tx264  : ${WIVRN_USE_X264}")
        message("")
        message("Audio backends:")
        message("\tPipewire  : ${WIVRN_USE_PIPEWIRE}")
        message("\tPulseaudio: ${WIVRN_USE_PULSEAUDIO}")
        message("")
        message("Optional features:")
        message("\tsystemd: ${WIVRN_USE_SYSTEMD}")
    endif()
endif()

if (WIVRN_BUILD_DASHBOARD)
    add_subdirectory(dashboard)
    set_target_properties(wivrn-dashboard PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/server)
endif()

add_subdirectory(tools)

foreach(TARGET_NAME wivrn wivrn-server wivrn-dashboard wivrn-common wivrn-dissector wivrnctl)
    if(TARGET ${TARGET_NAME})
        target_compile_options(${TARGET_NAME} PRIVATE
            -fdiagnostics-color -Wall -Wextra -pedantic
            -Wundef
            -Wno-missing-field-initializers
            -Wno-unused-parameter
            -Wno-comment
            -Wno-unused-variable
            -Wno-sign-compare
            $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile -Wno-array-bounds>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile -Wno-unneeded-internal-declaration -Wno-c99-extensions -Wno-unused-private-field>
        )

        if (WIVRN_WERROR)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()

    endif()
endforeach()
