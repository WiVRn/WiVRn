set(KTX_FEATURE_STATIC_LIBRARY ON)
set(KTX_FEATURE_TOOLS OFF)
set(KTX_FEATURE_TESTS OFF)
set(KTX_FEATURE_VK_UPLOAD ON)
set(KTX_FEATURE_GL_UPLOAD OFF)
set(KTX_FEATURE_LOADTEST_APPS OFF)

set(FASTGLTF_COMPILE_AS_CPP20 ON)

FetchContent_MakeAvailable(simdjson spdlog glm fastgltf imgui stb libktx implot)

if (NOT BOOST_FOUND)
    FetchContent_MakeAvailable(boost)
endif()

include(FreetypeHarfbuzz)
include(CompileGLSL)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

if(ANDROID)
    add_library(wivrn MODULE)

    find_library(ANDROID_LIBRARY NAMES android REQUIRED)
    find_library(MEDIA_LIBRARY NAMES mediandk REQUIRED)
    find_library(AAUDIO_LIBRARY NAMES aaudio REQUIRED)
    target_link_libraries(wivrn ${ANDROID_LIBRARY} ${MEDIA_LIBRARY} ${AAUDIO_LIBRARY})

    if (PICO)
        set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/external/pico)
        if(NOT EXISTS ${PICO_SDK_PATH})
            file(GLOB PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/PICO_OpenXR_SDK_*.zip)

            if("${PICO_SDK_ZIP}" STREQUAL "")
                message(FATAL_ERROR "Download the PICO OpenXR SDK from https://developer-global.pico-interactive.com/sdk in ${CMAKE_SOURCE_DIR}/PICO_OpenXR_SDK_*.zip")
            endif()

            file(ARCHIVE_EXTRACT
                    INPUT ${PICO_SDK_ZIP}
                    DESTINATION ${PICO_SDK_PATH}
                    PATTERNS include/openxr libs/android
                    VERBOSE)
        endif()

        add_library(OpenXR SHARED IMPORTED)
        target_include_directories(OpenXR INTERFACE
                ${PICO_SDK_PATH}/include)

        set_property(TARGET OpenXR
                PROPERTY IMPORTED_LOCATION ${PICO_SDK_PATH}/libs/android.${ANDROID_ABI}/libopenxr_loader.so)
        target_link_libraries(wivrn OpenXR)
    else()
        set(DYNAMIC_LOADER OFF)
        FetchContent_MakeAvailable(openxr_loader)
        target_link_libraries(wivrn openxr_loader)
    endif()


    target_link_libraries(wivrn native_app_glue)
    target_compile_definitions(wivrn PRIVATE -DXR_USE_PLATFORM_ANDROID -DVK_USE_PLATFORM_ANDROID_KHR)
    target_link_options(wivrn PRIVATE -Wl,--undefined=ANativeActivity_onCreate)

    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "audio/android/*.cpp"
        "decoder/android/*.cpp"
        )
    target_sources(wivrn PRIVATE ${PLATFORM_SOURCE})
else()
    add_executable(wivrn)

    if (OPENXR_FOUND)
        target_link_libraries(wivrn OpenXR::openxr_loader OpenXR::headers)
    else()
        FetchContent_MakeAvailable(openxr_loader)
        target_link_libraries(wivrn openxr_loader)
    endif()

    target_link_libraries(wivrn PkgConfig::LIBAV)


    file(GLOB PLATFORM_SOURCE CONFIGURE_DEPENDS
        "decoder/ffmpeg/*.cpp")
    target_sources(wivrn PRIVATE ${PLATFORM_SOURCE})

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_INSTALL_DATADIR}/wivrn)

    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/wivrn)
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/share/wivrn/assets SYMBOLIC)
endif()

target_sources(wivrn PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(wivrn PRIVATE ${imgui_SOURCE_DIR})
target_include_directories(wivrn PRIVATE ${implot_SOURCE_DIR})
# target_compile_definitions(wivrn PRIVATE "IMGUI_USER_CONFIG=\"render/imgui_config.h\"")
target_compile_definitions(wivrn PRIVATE IMGUI_USE_WCHAR32)
target_compile_definitions(wivrn PRIVATE IMGUI_ENABLE_FREETYPE)
# target_compile_definitions(wivrn PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS) # incompatible with implot
# target_compile_definitions(wivrn PRIVATE IMGUI_ENABLE_FREETYPE_LUNASVG)

target_compile_features(wivrn PRIVATE cxx_std_20)
target_compile_definitions(wivrn PRIVATE "XR_USE_TIMESPEC=1")

set_target_properties(wivrn PROPERTIES CXX_VISIBILITY_PRESET hidden)

file(GLOB LOCAL_SOURCE CONFIGURE_DEPENDS
    "*.cpp"
    "decoder/*.cpp"
    "scenes/*.cpp"
    "utils/*.cpp"
    "vk/*.cpp"
    "xr/*.cpp"
    "render/*.cpp"
    ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp
)

file(GLOB_RECURSE VULKAN_SHADERS CONFIGURE_DEPENDS "*.glsl")
target_sources(wivrn PRIVATE ${LOCAL_SOURCE} ${VULKAN_SHADERS})
wivrn_compile_glsl(wivrn ${VULKAN_SHADERS})

target_link_libraries(wivrn Vulkan::Vulkan spdlog::spdlog glm::glm fastgltf FreetypeHarfbuzz stb ktx_read Boost::locale)
target_compile_definitions(wivrn PRIVATE -DXR_USE_GRAPHICS_API_VULKAN)
target_compile_definitions(wivrn PRIVATE -DVMA_STATS_STRING_ENABLED=0)
target_compile_options(wivrn PRIVATE -fdiagnostics-color -Wall -Wextra -pedantic) # -Werror

target_compile_options(wivrn PRIVATE -Wno-missing-field-initializers -Wno-unused-parameter
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile>
)

target_include_directories(wivrn PRIVATE .)

if(NOT ANDROID)
    install(TARGETS wivrn)
endif()

target_link_libraries(wivrn simdjson wivrn-common wivrn-external)

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

find_program(GETTEXT_MSGFMT_COMMAND msgfmt REQUIRED)

set(MO_FILES)
file(GLOB PO_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/locale/*")
file(WRITE  ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "// generated file\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "#include <map>\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "#include <string>\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "#include <vector>\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "std::map<std::string, std::vector<int>> glyph_set_per_language = {\n")
foreach(PO_DIR IN LISTS PO_DIRS)
    cmake_path(GET PO_DIR FILENAME LANG)

    if(ANDROID)
        set(MO_DIR  ${CMAKE_ANDROID_ASSETS_DIRECTORIES}/locale/${LANG}/LC_MESSAGES)
    else()
        set(MO_DIR  ${CMAKE_CURRENT_BINARY_DIR}/locale/${LANG}/LC_MESSAGES)
    endif()
    set(MO_FILE ${MO_DIR}/wivrn.mo)
    set(PO_FILE ${PO_DIR}/wivrn.po)

    add_custom_command(OUTPUT ${MO_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MO_DIR}
        COMMAND ${GETTEXT_MSGFMT_COMMAND} ${PO_FILE} -o ${MO_FILE}
        DEPENDS ${PO_FILE})
    set(MO_FILES ${MO_FILES} ${MO_FILE})

    # Characters 0x20 to 0xff are always included
    execute_process(
        COMMAND python -c "print('{\"${LANG}\", {' + ', '.join([str(ord(c)) for c in sorted(set(open('wivrn.po').read())) if ord(c) > 255]) + '}},')"
        WORKING_DIRECTORY ${PO_DIR}
        # OUTPUT_QUIET
        OUTPUT_VARIABLE out
    )
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp ${out})

    install(FILES ${MO_FILE} DESTINATION ${CMAKE_INSTALL_DATADIR}/locale/${LANG}/LC_MESSAGES)

endforeach()
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/glyph_set.cpp "};\n")

add_custom_target(translations ALL DEPENDS ${MO_FILES})
add_dependencies(wivrn translations)

if(ANDROID)
    set(LICENSES_DIR "${CMAKE_ANDROID_ASSETS_DIRECTORIES}/licenses")
else()
    set(LICENSES_DIR "licenses")
endif()

file(MAKE_DIRECTORY "${LICENSES_DIR}")
file(COPY_FILE "${CMAKE_SOURCE_DIR}/COPYING" "${LICENSES_DIR}/WiVRn")
file(COPY_FILE "${simdjson_SOURCE_DIR}/LICENSE" "${LICENSES_DIR}/simdjson")
if (OPENXR_SOURCE_DIR)
    file(COPY_FILE "${OPENXR_SOURCE_DIR}/LICENSE" "${LICENSES_DIR}/openxr-loader")
endif()

install(DIRECTORY "${LICENSES_DIR}" DESTINATION "${CMAKE_INSTALL_DATADIR}/wivrn")

if(NOT ANDROID)
    set_target_properties(wivrn PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/wivrn)
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/assets         ${CMAKE_BINARY_DIR}/share/wivrn/assets SYMBOLIC)
    file(CREATE_LINK ${CMAKE_CURRENT_BINARY_DIR}/locale ${CMAKE_BINARY_DIR}/share/locale       SYMBOLIC)
endif()
