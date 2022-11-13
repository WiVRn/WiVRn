
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF)
set(TINYGLTF_HEADER_ONLY OFF)
set(TINYGLTF_INSTALL OFF)

FetchContent_MakeAvailable(spdlog glm tinygltf)

include(FreetypeHarfbuzz)
include(CompileGLSL)


if(ANDROID)
    add_library(wivrn MODULE)

    find_library(ANDROID_LIBRARY NAMES android REQUIRED)
    find_library(MEDIA_LIBRARY NAMES mediandk REQUIRED)
    target_link_libraries(wivrn ${ANDROID_LIBRARY} ${MEDIA_LIBRARY})

    if(OCULUS)
        set(OCULUS_SDK_PATH ${CMAKE_CURRENT_BINARY_DIR}/OculusSDK)

        if(NOT EXISTS ${OCULUS_SDK_PATH})
            file(GLOB OCULUS_SDK_ZIP ${CMAKE_SOURCE_DIR}/ovr_openxr_mobile_sdk_*.zip)

            if("${OCULUS_SDK_ZIP}" STREQUAL "")
                message(FATAL_ERROR "Download the Oculus SDK from https://developer.oculus.com/downloads/package/oculus-openxr-mobile-sdk in ${CMAKE_SOURCE_DIR}/ovr_openxr_mobile_sdk_*.zip")
            endif()

            file(ARCHIVE_EXTRACT INPUT ${OCULUS_SDK_ZIP} DESTINATION ${OCULUS_SDK_PATH})
        endif()

        add_library(OpenXR SHARED IMPORTED)
        target_include_directories(OpenXR INTERFACE ${OCULUS_SDK_PATH}/3rdParty/khronos/openxr/OpenXR-SDK/include)

        if(CMAKE_BUILD_TYPE STREQUAL Debug)
            set_property(TARGET OpenXR
                PROPERTY IMPORTED_LOCATION ${OCULUS_SDK_PATH}/OpenXR/Libs/Android/${ANDROID_ABI}/Debug/libopenxr_loader.so)
        else()
            set_property(TARGET OpenXR
                PROPERTY IMPORTED_LOCATION ${OCULUS_SDK_PATH}/OpenXR/Libs/Android/${ANDROID_ABI}/Release/libopenxr_loader.so)
        endif()

    else()
        message(FATAL_ERROR "TODO: implement non-oculus android headsets")
    endif()

    target_link_libraries(wivrn native_app_glue OpenXR)
    target_compile_definitions(wivrn PRIVATE -DXR_USE_PLATFORM_ANDROID -DVK_USE_PLATFORM_ANDROID_KHR)
    target_link_options(wivrn PRIVATE -Wl,--undefined=ANativeActivity_onCreate)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "src/decoder/android/*.cpp")
    target_sources(wivrn PRIVATE ${PLATFORM_SOURCE})
else()
    add_executable(wivrn)
    target_compile_definitions(wivrn PRIVATE -DXR_USE_PLATFORM_XLIB)

    find_package(OpenXR REQUIRED)
    target_link_libraries(wivrn OpenXR::openxr_loader OpenXR::headers PkgConfig::LIBAV)


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
compile_glsl(wivrn ${VULKAN_SHADERS})

target_link_libraries(wivrn Vulkan::Vulkan spdlog::spdlog glm::glm tinygltf FreetypeHarfbuzz)
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

if(USE_ANDROID_VALIDATION_LAYER)
    set(VVL_VERSION "1.3.231.1")

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip)
        if (EXISTS ${CMAKE_SOURCE_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/sdk-${VVL_VERSION}/android-binaries-${VVL_VERSION}.zip ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip
                EXPECTED_HASH SHA256=eec3a54222d6d33d4f787a13a45c60ac5b9e8d7bf795fc41e4242354a02e34ba)
        endif()
    endif()

    set(VVL_ZIPFILE ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip)

    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayers-${VVL_VERSION}.zip DESTINATION ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayer)

    add_library(vvl1 SHARED IMPORTED)
    set_property(TARGET vvl1 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayer/${CMAKE_ANDROID_ARCH_ABI}/libVkLayer_khronos_validation.so)
    add_library(vvl2 SHARED IMPORTED)
    set_property(TARGET vvl2 PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/Vulkan-ValidationLayer/${CMAKE_ANDROID_ARCH_ABI}/libc++_shared.so)

    target_link_libraries(wivrn vvl1 vvl2) # Hack to get gradle to package the validation layer in the APK
endif()
