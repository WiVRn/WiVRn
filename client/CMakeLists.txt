set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF)
set(TINYGLTF_HEADER_ONLY OFF)
set(TINYGLTF_INSTALL OFF)

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
    set(HARFBUZZ_VERSION 5.3.1)
    set(FREETYPE_VERSION 2.12.1)

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
        endif()
    endif()

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VERSION}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz DESTINATION ${CMAKE_BINARY_DIR})
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz DESTINATION ${CMAKE_BINARY_DIR})

    add_library(freetypeharfbuzz STATIC
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/harfbuzz.cc
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/hb-ft.cc
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/autofit/autofit.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbase.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbbox.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbitmap.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftcid.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftfstype.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgasp.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftglyph.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgxval.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftinit.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftmm.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftotval.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpatent.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpfr.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftstroke.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftsynth.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/fttype1.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftwinfnt.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/bdf/bdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/bzip2/ftbzip2.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cache/ftcache.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cff/cff.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cid/type1cid.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/gzip/ftgzip.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/lzw/ftlzw.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pcf/pcf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pfr/pfr.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/psaux/psaux.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pshinter/pshinter.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/psnames/psnames.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/raster/raster.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/sdf/sdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/sfnt/sfnt.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/smooth/smooth.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/svg/svg.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/truetype/truetype.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/type1/type1.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/type42/type42.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/winfonts/winfnt.c

        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftdebug.c
    )

    target_sources(freetypeharfbuzz PRIVATE
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/builds/unix/ftsystem.c)

    target_include_directories(freetypeharfbuzz PUBLIC
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/include
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src)

    target_compile_definitions(freetypeharfbuzz PRIVATE -DFT2_BUILD_LIBRARY=1 -DHAVE_FREETYPE=1 -DHAVE_UNISTD_H=1 -DHAVE_FCNTL_H=1)

    set(deps_freetype freetypeharfbuzz)
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

include(CompileGLSL)

if(ANDROID)
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

    add_library(wivrn MODULE)
    target_link_libraries(wivrn ${ANDROID_LIBRARY})
    target_link_libraries(wivrn ${MEDIA_LIBRARY})
    target_link_libraries(wivrn native_app_glue)
    target_link_libraries(wivrn OpenXR)
    target_link_libraries(wivrn Boost::pfr)
    target_compile_definitions(wivrn PRIVATE -DXR_USE_PLATFORM_ANDROID -DVK_USE_PLATFORM_ANDROID_KHR)
    target_link_options(wivrn PRIVATE -Wl,--undefined=ANativeActivity_onCreate)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "src/decoder/android/*.cpp")
    target_sources(wivrn PRIVATE ${PLATFORM_SOURCE})
else()
    find_package(OpenXR REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil
        libswscale)

    add_executable(wivrn)
    target_compile_definitions(wivrn PRIVATE -DXR_USE_PLATFORM_XLIB)
    target_link_libraries(wivrn OpenXR::openxr_loader
        OpenXR::headers PkgConfig::LIBAV)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "src/decoder/ffmpeg/*.cpp")
    target_sources(wivrn PRIVATE ${PLATFORM_SOURCE})
endif()

target_compile_features(wivrn PRIVATE cxx_std_20)
target_compile_definitions(wivrn PRIVATE "XR_USE_TIMESPEC=1")
set_target_properties(wivrn PROPERTIES CXX_VISIBILITY_PRESET hidden)

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

target_sources(wivrn PRIVATE ${LOCAL_SOURCE} ${VULKAN_SHADERS})

target_link_libraries(wivrn Vulkan::Vulkan spdlog::spdlog glm::glm tinygltf ${deps_freetype} ${deps_vvl})
target_compile_definitions(wivrn PRIVATE -DXR_USE_GRAPHICS_API_VULKAN)
target_compile_options(wivrn PRIVATE -fdiagnostics-color -Wall -Wextra -pedantic) # -Werror

target_compile_options(wivrn PRIVATE -Wno-missing-field-initializers -Wno-unused-parameter
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile>
)

target_include_directories(wivrn PRIVATE src)

target_link_libraries(wivrn wivrn-common)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/vk_layer_settings.txt
"khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG,VK_DBG_LAYER_ACTION_BREAK
khronos_validation.report_flags = error,warn
khronos_validation.log_filename = stdout
khronos_validation.disables =
khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_THREAD_SAFETY_EXT
")
endif()

compile_glsl(wivrn ${VULKAN_SHADERS})
