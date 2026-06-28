# Pico Native client - for Pico Neo 2 using PvrSDK-Native (GLES-based)

# OpenXR headers - needed for XrPosef, XrFovf, XrTime types used in wivrn_packets.h
set(DYNAMIC_LOADER ON)
FetchContent_MakeAvailable(spdlog openxr_loader)

# PvrSDK-Native library (from AAR)
set(PVR_LIB_PATH ${CMAKE_SOURCE_DIR}/external/pvrsdk-native/jni/${ANDROID_ABI}/libPvr_NativeSDK.so)
if(NOT EXISTS ${PVR_LIB_PATH})
    message(FATAL_ERROR
        "PvrSDK-Native not found.\n"
        "The Pico Neo 2 SDK is required to build the Pico native client.\n"
        "Download it from https://developer.picoxr.com/ (PvrSDK-Native 2.8.5.4 or compatible)\n"
        "and extract to external/pvrsdk-native/ so that the path matches:\n"
        "  external/pvrsdk-native/jni/${ANDROID_ABI}/libPvr_NativeSDK.so"
    )
endif()
add_library(Pvr_NativeSDK SHARED IMPORTED)
set_target_properties(Pvr_NativeSDK PROPERTIES
    IMPORTED_LOCATION ${PVR_LIB_PATH}
)

# Client module
add_library(wivrn-neo2 MODULE)

target_sources(wivrn-neo2 PRIVATE
    main.cpp
    wivrn_client_pico.cpp
    pico_decoder.cpp
    pico_audio.cpp
    pico_blit.cpp
)

target_include_directories(wivrn-neo2 PRIVATE
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/external/pico-native-sdk/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(wivrn-neo2
    wivrn-common
    openxr_loader
    Pvr_NativeSDK
    spdlog::spdlog
)

# Android libraries
find_library(ANDROID_LIBRARY NAMES android REQUIRED)
find_library(MEDIA_LIBRARY NAMES mediandk REQUIRED)
find_library(AAUDIO_LIBRARY NAMES aaudio REQUIRED)
find_library(LOG_LIBRARY NAMES log REQUIRED)
find_library(GLES_LIBRARY NAMES GLESv2 REQUIRED)
find_library(EGL_LIBRARY NAMES EGL REQUIRED)
target_link_libraries(wivrn-neo2
    ${ANDROID_LIBRARY}
    ${MEDIA_LIBRARY}
    ${AAUDIO_LIBRARY}
    ${LOG_LIBRARY}
    ${GLES_LIBRARY}
    ${EGL_LIBRARY}
)

# Copy Pvr_NativeSDK.so to output
get_target_property(pvr_lib_path Pvr_NativeSDK IMPORTED_LOCATION)
add_custom_command(TARGET wivrn-neo2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${pvr_lib_path} $<TARGET_FILE_DIR:wivrn-neo2>
)

target_compile_definitions(wivrn-neo2 PRIVATE
    -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
)
