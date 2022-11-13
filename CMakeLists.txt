cmake_minimum_required(VERSION 3.19)
project(WiVRn VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.24)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
include(ExternalProject)

option(WIVRN_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
option(WIVRN_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WIVRN_BUILD_CLIENT "Build WiVRn client" ON)
option(WIVRN_BUILD_SERVER "Build WiVRn server" ON)

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)

if (WIVRN_BUILD_SERVER)
    find_package(CUDAToolkit)
    pkg_check_modules(LIBAV IMPORTED_TARGET libavcodec libavutil libswscale libavfilter)
    pkg_check_modules(X264  IMPORTED_TARGET x264)

    if(CUDAToolkit_FOUND)
        set(WIVRN_HAVE_CUDA ON)
    endif()

    if(LIBAV_FOUND)
        set(WIVRN_HAVE_FFMPEG ON)
    endif()

    if(X264_FOUND)
        set(WIVRN_HAVE_X264 ON)
    endif()

    if (NOT CUDAToolkit_FOUND AND NOT LIBAV_FOUND AND NOT X264_FOUND)
        message(FATAL_ERROR "No encoder found")
    endif()

    pkg_check_modules(AVAHI IMPORTED_TARGET avahi-client)
    find_package(Eigen3 REQUIRED)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ANDROID)
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

FetchContent_Declare(boostpfr URL https://github.com/boostorg/pfr/archive/refs/tags/2.0.3.tar.gz)
FetchContent_MakeAvailable(boostpfr)

add_subdirectory(common)

if (WIVRN_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WIVRN_BUILD_SERVER)
    add_subdirectory(server)
endif()


get_property(importTargets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY IMPORTED_TARGETS)
get_property(buildsystemTargets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
message("Imported targets: ${importTargets}")
message("Build system targets: ${buildsystemTargets}")
