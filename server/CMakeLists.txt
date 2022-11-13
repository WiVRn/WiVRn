
set(XRT_BUILD_DRIVER_ARDUINO         OFF)
set(XRT_BUILD_DRIVER_DAYDREAM        OFF)
set(XRT_BUILD_DRIVER_EUROC           OFF)
set(XRT_BUILD_DRIVER_HDK             OFF)
set(XRT_BUILD_DRIVER_HYDRA           OFF)
set(XRT_BUILD_DRIVER_NS              OFF)
set(XRT_BUILD_DRIVER_OHMD            OFF)
set(XRT_BUILD_DRIVER_OPENGLOVES      OFF)
set(XRT_BUILD_DRIVER_PSMV            OFF)
set(XRT_BUILD_DRIVER_PSVR            OFF)
set(XRT_BUILD_DRIVER_QWERTY          OFF)
set(XRT_BUILD_DRIVER_REMOTE          OFF)
set(XRT_BUILD_DRIVER_RIFT_S          OFF)
set(XRT_BUILD_DRIVER_SIMULATED       ON)
set(XRT_BUILD_DRIVER_TWRAP           OFF)
set(XRT_BUILD_DRIVER_VF              OFF)
set(XRT_BUILD_DRIVER_VIVE            OFF)
set(XRT_BUILD_DRIVER_WMR             OFF)
set(XRT_BUILD_SAMPLES                OFF)

set(XRT_FEATURE_COMPOSITOR_NULL      OFF)
set(XRT_FEATURE_SERVICE              ON)
set(XRT_FEATURE_SERVICE_SYSTEMD      OFF)
set(XRT_FEATURE_SLAM                 OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN       OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN       OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)

set(XRT_HAVE_SDL2                    OFF)

set(BUILD_DOC                        OFF)
set(BUILD_TESTING                    OFF)

set(XRT_IPC_MSG_SOCK_FILENAME "wivrn_comp_ipc" CACHE STRING "Service socket filename")
set(XRT_PID_FILE_NAME         "wivrn.pid"      CACHE STRING "Service pidfile filename")
set(XRT_RUNTIME_SUFFIX        "wivrn"          CACHE STRING "OpenXR client library suffix")

FetchContent_GetProperties(monado)
if(NOT monado_POPULATED)
  # Fetch the content using previously declared details
  FetchContent_Populate(monado)

  # Bring the populated content into the build
  add_subdirectory(${monado_SOURCE_DIR} ${monado_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()


add_executable(monado-wivrn
		main.cpp
		target_instance_wivrn.cpp
		hostname.cpp
		avahi_publisher.cpp

		driver/encoder_settings.cpp
		driver/wivrn_hmd.cpp
		driver/wivrn_comp_target.cpp
		driver/wivrn_controller.cpp
		driver/pose_list.cpp
		driver/video_encoder.cpp
		driver/view_list.cpp
		driver/wivrn_session.cpp
		driver/wivrn_connection.cpp
		driver/xrt_cast.cpp
		driver/external/rs.c
		${WIVRN_SHADER_HEADERS}
		driver/yuv_converter.cpp
	)

target_include_directories(monado-wivrn SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/compositor/)

if(CUDAToolkit_FOUND)
        target_sources(monado-wivrn PRIVATE driver/video_encoder_nvenc.cpp)
        target_link_libraries(monado-wivrn PRIVATE CUDA::cuda_driver nvidia-encode)
endif()

if(LIBAV_FOUND)
        target_sources(
                monado-wivrn
                PRIVATE driver/ffmpeg/VideoEncoderFFMPEG.cpp driver/ffmpeg/VideoEncoderVA.cpp
                        driver/ffmpeg/ffmpeg_helper.cpp
                )
        target_link_libraries(monado-wivrn PRIVATE PkgConfig::LIBAV)
endif()

if(X264_FOUND)
        target_sources(monado-wivrn PRIVATE driver/video_encoder_x264.cpp)
        target_link_libraries(monado-wivrn PRIVATE PkgConfig::X264)
endif()


include(CompileGLSL)

file(GLOB VULKAN_SHADERS CONFIGURE_DEPENDS "driver/shaders/*.glsl")
target_sources(monado-wivrn PRIVATE ${LOCAL_SOURCE} ${VULKAN_SHADERS})
compile_glsl(monado-wivrn ${VULKAN_SHADERS})

target_link_libraries(
	monado-wivrn
	PRIVATE
		aux_os
		aux_util
		aux_vk
		comp_main
		ipc_server
		xrt-external-openxr
		xrt-interfaces

		systemd
		Boost::pfr
		PkgConfig::AVAHI
		Eigen3::Eigen
		wivrn-common
	)

install(TARGETS monado-wivrn)

set_target_properties(${RUNTIME_TARGET} PROPERTIES EXCLUDE_FROM_ALL FALSE)
install (SCRIPT
	${monado_BINARY_DIR}/src/xrt/targets/openxr/active_runtime.cmake)
generate_openxr_runtime_manifest_at_install(
	RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${CMAKE_INSTALL_LIBDIR}
	RUNTIME_TARGET ${RUNTIME_TARGET}
	DESTINATION share/openxr/1
	RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_LIBDIR}
	)
install(TARGETS ${RUNTIME_TARGET}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime)
