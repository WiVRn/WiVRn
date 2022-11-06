cmake_minimum_required(VERSION 3.19)
project(WiVRn VERSION 1.0 LANGUAGES C CXX)


option(USE_SYSTEM_FREETYPE "Use system FreeType" ON)
option(USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF)
set(TINYGLTF_HEADER_ONLY OFF)
set(TINYGLTF_INSTALL OFF)

include(FetchContent)
include(ExternalProject)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.10.0)

FetchContent_Declare(boostpfr
    GIT_REPOSITORY https://github.com/boostorg/pfr.git
    GIT_TAG        69c5692cd666b1685a5eef00a4a79ca2e6093ff0)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8)

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.6.3)

if (USE_SYSTEM_FREETYPE)
    find_package(Freetype REQUIRED)
    find_package(harfbuzz REQUIRED)
    set(deps_freetype Freetype::Freetype harfbuzz::harfbuzz)
else()
    FetchContent_Declare(
        freetype
        GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
        GIT_TAG        VER-2-12-1
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DFT_DISABLE_ZLIB=TRUE
        -DFT_DISABLE_BZIP2=TRUE
        -DFT_DISABLE_PNG=TRUE
        # -DFT_DISABLE_HARFBUZZ=TRUE
        -DFT_REQUIRE_HARFBUZZ=TRUE
        -DFT_DISABLE_BROTLI=TRUE
        -DBUILD_SHARED_LIBS=TRUE)

    FetchContent_Declare(
        harfbuzz
        # DEPENDS freetype_without_harfbuzz
        GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
        GIT_TAG        5.3.1
        CMAKE_ARGS -DHB_HAVE_FREETYPE=ON)

    set(deps_freetype freetype harfbuzz)
    set(fetch_content freetype harfbuzz)
endif()

if(USE_ANDROID_VALIDATION_LAYER)
    set(VVL_VERSION "1.3.231.1")
    set(VVL_ZIPFILE ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip)

    if(NOT EXISTS ${VVL_ZIPFILE})
        file(DOWNLOAD https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/sdk-${VVL_VERSION}/android-binaries-${VVL_VERSION}.zip
            ${VVL_ZIPFILE})
    endif()

    file(ARCHIVE_EXTRACT INPUT ${VVL_ZIPFILE} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-ValidationLayer)

    add_library(vvl1 SHARED IMPORTED)
    set_property(TARGET vvl1 PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-ValidationLayer/${CMAKE_ANDROID_ARCH_ABI}/libVkLayer_khronos_validation.so)
    add_library(vvl2 SHARED IMPORTED)
    set_property(TARGET vvl2 PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-ValidationLayer/${CMAKE_ANDROID_ARCH_ABI}/libc++_shared.so)
    set(deps_vvl vvl1 vvl2) # Hack to get gradle to package the validation layer in the APK
endif()

FetchContent_MakeAvailable(spdlog boostpfr glm tinygltf ${fetch_content})

FetchContent_GetProperties(tinygltf SOURCE_DIR TinyGLTF_SOURCE_DIR)

find_package(Vulkan REQUIRED)

get_property(importTargets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY IMPORTED_TARGETS)
get_property(buildsystemTargets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
message("Imported targets: ${importTargets}")
message("Build system targets: ${buildsystemTargets}")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompileGLSL)

if(ANDROID)
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
    find_library(ANDROID_LIBRARY NAMES android)

    find_library(MEDIA_LIBRARY NAMES mediandk)

    if(OCULUS_SDK)
        add_library(OpenXR SHARED IMPORTED)
        target_include_directories(OpenXR INTERFACE ${OCULUS_SDK}/3rdParty/khronos/openxr/OpenXR-SDK/include)

        if(CMAKE_BUILD_TYPE STREQUAL Debug)
            set_property(TARGET OpenXR
                PROPERTY IMPORTED_LOCATION ${OCULUS_SDK}/OpenXR/Libs/Android/${ANDROID_ABI}/Debug/libopenxr_loader.so)
        else()
            set_property(TARGET OpenXR
                PROPERTY IMPORTED_LOCATION ${OCULUS_SDK}/OpenXR/Libs/Android/${ANDROID_ABI}/Release/libopenxr_loader.so)
        endif()
    else()
        message(FATAL_ERROR "TODO: implement non-oculus android headsets")
    endif()

    add_library(${PROJECT_NAME} MODULE)
    target_link_libraries(${PROJECT_NAME} ${ANDROID_LIBRARY})
    target_link_libraries(${PROJECT_NAME} ${MEDIA_LIBRARY})
    target_link_libraries(${PROJECT_NAME} native_app_glue)
    target_link_libraries(${PROJECT_NAME} OpenXR)
    target_link_libraries(${PROJECT_NAME} Boost::pfr)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DXR_USE_PLATFORM_ANDROID -DVK_USE_PLATFORM_ANDROID_KHR)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--undefined=ANativeActivity_onCreate)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "src/decoder/android/*.cpp")
    target_sources(${PROJECT_NAME} PRIVATE ${PLATFORM_SOURCE})
else()
    find_package(OpenXR REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil
        libswscale)

    add_executable(${PROJECT_NAME})
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DXR_USE_PLATFORM_XLIB)
    target_link_libraries(${PROJECT_NAME} OpenXR::openxr_loader
        OpenXR::headers PkgConfig::LIBAV)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "src/decoder/ffmpeg/*.cpp")
    target_sources(${PROJECT_NAME} PRIVATE ${PLATFORM_SOURCE})
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJECT_NAME} PRIVATE "XR_USE_TIMESPEC=1")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)

file(GLOB LOCAL_SOURCE CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/decoder/*.cpp"
    "src/external/*.c"
    "src/scenes/*.cpp"
    "src/utils/*.cpp"
    "src/vk/*.cpp"
    "src/xr/*.cpp"
    "src/render/*.cpp")
file(GLOB_RECURSE VULKAN_SHADERS CONFIGURE_DEPENDS "src/*.glsl")

target_sources(${PROJECT_NAME} PRIVATE ${LOCAL_SOURCE} ${VULKAN_SHADERS})

target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan spdlog::spdlog glm::glm tinygltf ${deps_freetype} ${deps_vvl})
target_compile_definitions(${PROJECT_NAME} PRIVATE -DXR_USE_GRAPHICS_API_VULKAN)
target_compile_options(${PROJECT_NAME} PRIVATE -fdiagnostics-color -Wall -Wextra -pedantic) # -Werror

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-missing-field-initializers -Wno-unused-parameter
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile>
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

execute_process(COMMAND git describe --always OUTPUT_VARIABLE GIT_VERSION)
string(STRIP ${GIT_VERSION} GIT_VERSION)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.h
    COMMAND ${CMAKE_COMMAND}
        -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
        -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
        -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
)
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.h)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/vk_layer_settings.txt
"khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG,VK_DBG_LAYER_ACTION_BREAK
khronos_validation.report_flags = error,warn
khronos_validation.log_filename = stdout
khronos_validation.disables =
khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_THREAD_SAFETY_EXT
")
endif()

compile_glsl(${PROJECT_NAME} ${VULKAN_SHADERS})
