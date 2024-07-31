
set(XRT_BUILD_DRIVER_ARDUINO            OFF)
set(XRT_BUILD_DRIVER_DAYDREAM           OFF)
set(XRT_BUILD_DRIVER_EUROC              OFF)
set(XRT_BUILD_DRIVER_HDK                OFF)
set(XRT_BUILD_DRIVER_HYDRA              OFF)
set(XRT_BUILD_DRIVER_NS                 OFF)
set(XRT_BUILD_DRIVER_OHMD               OFF)
set(XRT_BUILD_DRIVER_OPENGLOVES         OFF)
set(XRT_BUILD_DRIVER_PSMV               OFF)
set(XRT_BUILD_DRIVER_PSSENSE            OFF)
set(XRT_BUILD_DRIVER_PSVR               OFF)
set(XRT_BUILD_DRIVER_QWERTY             OFF)
set(XRT_BUILD_DRIVER_REMOTE             OFF)
set(XRT_BUILD_DRIVER_RIFT_S             OFF)
set(XRT_BUILD_DRIVER_ROKID              OFF)
set(XRT_BUILD_DRIVER_SIMULATED          OFF)
set(XRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE OFF)
set(XRT_BUILD_DRIVER_TWRAP              OFF)
set(XRT_BUILD_DRIVER_VF                 OFF)
set(XRT_BUILD_DRIVER_VIVE               OFF)
set(XRT_BUILD_DRIVER_WMR                OFF)
set(XRT_BUILD_DRIVER_XREAL_AIR          OFF)
set(XRT_BUILD_SAMPLES                   OFF)

set(XRT_FEATURE_COMPOSITOR_NULL      OFF)
set(XRT_FEATURE_SERVICE              ON)
set(XRT_FEATURE_SERVICE_SYSTEMD      OFF)
set(XRT_FEATURE_SLAM                 OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN       OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN       OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)
set(XRT_FEATURE_WINDOW_PEEK          OFF)

set(XRT_HAVE_BLUETOOTH               OFF)
set(XRT_HAVE_GST                     OFF)

set(XRT_HAVE_JPEG                    OFF)
set(XRT_HAVE_SDL2                    OFF)
set(XRT_HAVE_OPENCV                  OFF)
set(XRT_HAVE_DBUS                    OFF)
set(XRT_HAVE_LIBUDEV                 OFF)

set(XRT_HAVE_SYSTEMD ${WIVRN_USE_SYSTEMD})
set(XRT_INSTALL_SYSTEMD_UNIT_FILES   OFF)

set(BUILD_DOC                        OFF)
set(BUILD_TESTING                    OFF)

set(XRT_HAVE_SYSTEM_CJSON            OFF)

set(XRT_IPC_MSG_SOCK_FILENAME     "wivrn_comp_ipc" CACHE STRING "Service socket filename")
set(XRT_IPC_SERVICE_PID_FILENAME  "wivrn.pid"      CACHE STRING "Service pidfile filename")
set(XRT_OXR_RUNTIME_SUFFIX        "wivrn"          CACHE STRING "OpenXR client library suffix")

FetchContent_MakeAvailable(monado)

add_executable(wivrn-server
		accept_connection.cpp
		active_runtime.cpp
		avahi_publisher.cpp
		hostname.cpp
		main.cpp
		start_application.cpp
		target_instance_wivrn.cpp
		wivrn_ipc.cpp

		audio/audio_setup.cpp

		encoder/encoder_settings.cpp
		encoder/video_encoder.cpp
		encoder/yuv_converter.cpp

		driver/clock_offset.cpp
		driver/configuration.cpp
		driver/wivrn_hmd.cpp
		driver/wivrn_pacer.cpp
		driver/wivrn_comp_target.cpp
		driver/wivrn_controller.cpp
		driver/wivrn_eye_tracker.cpp
		driver/pose_list.cpp
		driver/view_list.cpp
		driver/hand_joints_list.cpp
		driver/wivrn_session.cpp
		driver/wivrn_connection.cpp
		driver/xrt_cast.cpp

		utils/wivrn_vk_bundle.cpp

		${WIVRN_SHADER_HEADERS}
	)
target_compile_features(wivrn-server PRIVATE cxx_std_20)
target_compile_definitions(wivrn-server PRIVATE VULKAN_HPP_NO_CONSTRUCTORS)

target_include_directories(wivrn-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/compositor/)
target_include_directories(wivrn-server PRIVATE .)

target_link_libraries(wivrn-server PRIVATE CLI11::CLI11)

if(WIVRN_USE_NVENC)
        target_sources(wivrn-server PRIVATE encoder/video_encoder_nvenc.cpp)
endif()

if(WIVRN_USE_VAAPI)
        target_sources(
                wivrn-server
                PRIVATE encoder/ffmpeg/video_encoder_ffmpeg.cpp
                        encoder/ffmpeg/video_encoder_va.cpp
                        encoder/ffmpeg/ffmpeg_helper.cpp
                )
        target_link_libraries(wivrn-server PRIVATE PkgConfig::LIBAV PkgConfig::LIBDRM)
endif()

if(WIVRN_USE_X264)
        target_sources(wivrn-server PRIVATE encoder/video_encoder_x264.cpp)
        target_link_libraries(wivrn-server PRIVATE PkgConfig::X264)
endif()

if(WIVRN_USE_SYSTEMD)
        target_link_libraries(wivrn-server PRIVATE PkgConfig::SYSTEMD)
endif()

if(WIVRN_USE_PIPEWIRE)
	target_sources(
		wivrn-server
		PRIVATE audio/audio_pipewire.cpp
		)
	target_link_libraries(wivrn-server PRIVATE PkgConfig::libpipewire)
endif()

if(WIVRN_USE_PULSEAUDIO)
	target_sources(
		wivrn-server
		PRIVATE audio/audio_pulse.cpp
		)
	target_link_libraries(wivrn-server PRIVATE PkgConfig::libpulse)
endif()

include(CompileGLSL)

file(GLOB VULKAN_SHADERS CONFIGURE_DEPENDS "encoder/shaders/*.glsl")
target_sources(wivrn-server PRIVATE ${LOCAL_SOURCE} ${VULKAN_SHADERS})
wivrn_compile_glsl(wivrn-server ${VULKAN_SHADERS})

target_link_libraries(
	wivrn-server
	PRIVATE
		aux_os
		aux_util
		aux_vk
		comp_main
		ipc_server
		xrt-external-openxr
		xrt-interfaces

		PkgConfig::AVAHI
		Eigen3::Eigen
		wivrn-common
		wivrn-external
		nlohmann_json::nlohmann_json
	)

configure_file(dist/wivrn.service.in wivrn.service)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wivrn.service
	DESTINATION lib/systemd/user/)

configure_file(dist/wivrn-application.service.in wivrn-application.service)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wivrn-application.service
	DESTINATION lib/systemd/user/)

install(FILES dist/firewalld-wivrn.xml
	RENAME wivrn.xml
	DESTINATION lib/firewalld/services)

install(TARGETS wivrn-server)

set_target_properties(${RUNTIME_TARGET} PROPERTIES EXCLUDE_FROM_ALL FALSE)

set(_extra_args)
if(WIVRN_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH)
set(_extra_args ABSOLUTE_RUNTIME_PATH)
else()
set(_extra_args RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${CMAKE_INSTALL_LIBDIR}/wivrn)
endif()

generate_openxr_runtime_manifest_at_install(
	${_extra_args}
	RUNTIME_TARGET ${RUNTIME_TARGET}
	DESTINATION share/openxr/1
	RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_LIBDIR}/wivrn
	)

install(TARGETS ${RUNTIME_TARGET}
	LIBRARY
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/wivrn
	COMPONENT Runtime)
