cmake_minimum_required(VERSION 3.20)
project(WiVRn VERSION 0.0.0 LANGUAGES C CXX)

# Set default for FetchContent projects
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
include(ExternalProject)
include(GNUInstallDirs)
include("cmake/AutoOption.cmake")

option(WIVRN_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
auto_option(WIVRN_USE_SYSTEM_BOOST "Use system Boost headers" AUTO)
auto_option(WIVRN_USE_SYSTEMD "Use libsystemd, required to show pretty hostname" AUTO)
option(WIVRN_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WIVRN_BUILD_CLIENT "Build WiVRn client" ON)
option(WIVRN_BUILD_SERVER "Build WiVRn server" ON)

auto_option(WIVRN_USE_NVENC "Enable nvenc (Nvidia) hardware encoder" AUTO)
auto_option(WIVRN_USE_VAAPI "Enable vaapi (AMD/Intel) hardware encoder" AUTO)
auto_option(WIVRN_USE_X264 "Enable x264 software encoder" AUTO)

option(ENABLE_COLOURED_OUTPUT "Always produce ANSI-coloured output (GNU/Clang only)." ON)

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


if (WIVRN_USE_SYSTEM_BOOST STREQUAL "AUTO")
    find_package(Boost 1.75.0)
elseif(WIVRN_USE_SYSTEM_BOOST STREQUAL "ON")
    find_package(Boost 1.75.0 REQUIRED)
endif()

if (WIVRN_BUILD_SERVER)
    if (NOT WIVRN_USE_NVENC AND NOT WIVRN_USE_VAAPI AND NOT WIVRN_USE_X264)
        message(FATAL_ERROR "No encoder selected, use at least one of WIVRN_USE_NVENC, WIVRN_USE_VAAPI or WIVRN_USE_X264")
    endif()

    if (WIVRN_USE_NVENC STREQUAL "AUTO")
        find_package(CUDAToolkit)
        if (CUDAToolkit_FOUND)
            set(WIVRN_USE_NVENC ON)
        else()
            set(WIVRN_USE_NVENC OFF)
        endif()
    elseif(WIVRN_USE_NVENC)
        find_package(CUDAToolkit REQUIRED)
    endif()

    if (WIVRN_USE_VAAPI STREQUAL "AUTO")
        pkg_check_modules(LIBAV IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
        if (LIBAV_FOUND)
            set(WIVRN_USE_VAAPI ON)
        else()
            set(WIVRN_USE_VAAPI OFF)
        endif()
    elseif (WIVRN_USE_VAAPI)
        pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
    endif()

    if (WIVRN_USE_X264 STREQUAL "AUTO")
        pkg_check_modules(X264 IMPORTED_TARGET x264)
        if (X264_FOUND)
            set(WIVRN_USE_X264 ON)
        else()
            set(WIVRN_USE_X264 OFF)
        endif()
    elseif (WIVRN_USE_X264)
        pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
    endif()

    if (WIVRN_USE_SYSTEMD STREQUAL "AUTO")
        pkg_check_modules(SYSTEMD IMPORTED_TARGET systemd)
        if (SYSTEMD_FOUND)
            set(WIVRN_USE_SYSTEMD ON)
        else()
            set(WIVRN_USE_SYSTEMD OFF)
        endif()
    elseif (WIVRN_USE_SYSTEMD)
        pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET systemd)
    endif()

    pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)
    pkg_check_modules(libpulse REQUIRED IMPORTED_TARGET libpulse)
    find_package(Eigen3 REQUIRED)
    find_package(nlohmann_json REQUIRED)
endif()

if (WIVRN_BUILD_CLIENT AND NOT ANDROID)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ANDROID)
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

# Common dependencies
FetchContent_Declare(boostpfr      URL https://github.com/boostorg/pfr/archive/refs/tags/2.0.3.tar.gz)
# Client dependencies
FetchContent_Declare(fastgltf      URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.6.1.tar.gz)
FetchContent_Declare(glm           URL https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.tar.gz)
FetchContent_Declare(openxr_loader URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.0.32.tar.gz)
FetchContent_Declare(spdlog        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.11.0.tar.gz)
FetchContent_Declare(stb           URL https://github.com/nothings/stb/archive/refs/heads/master.zip)
FetchContent_Declare(libktx        URL https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/v4.3.0-beta1.tar.gz)

# Server dependencies
FetchContent_Declare(monado
    GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado
    GIT_TAG          479973e68f02c769edbb657f9a4e52eb51d35391
    )

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.1.tar.gz
# Use the docking branch of imgui to use multi-viewport
    # GIT_REPOSITORY   https://github.com/ocornut/imgui.git
    # GIT_TAG          ce0d0ac8298ce164b5d862577e8b087d92f6e90e
    )

add_subdirectory(external)
add_subdirectory(common)

if (WIVRN_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WIVRN_BUILD_SERVER)
    add_subdirectory(server)

    message("Selected encoders:")
    message("\tNVENC: ${WIVRN_USE_NVENC}")
    message("\tVAAPI: ${WIVRN_USE_VAAPI}")
    message("\tx264:  ${WIVRN_USE_X264}")
    message("")
    message("Optional features:")
    message("\tsystemd: ${WIVRN_USE_SYSTEMD}")
endif()
