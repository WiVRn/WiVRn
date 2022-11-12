cmake_minimum_required(VERSION 3.19)
project(WiVRn VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

include(FetchContent)
include(ExternalProject)

option(WIVRN_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
option(WIVRN_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WIVRN_BUILD_CLIENT "Build WiVRn client" ON)
option(WIVRN_BUILD_SERVER "Build WiVRn server" OFF)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ANDROID)
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

add_subdirectory(common)

if (WIVRN_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WIVRN_BUILD_SERVER)
    add_subdirectory(server)
endif()

