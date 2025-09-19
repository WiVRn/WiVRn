
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
set(XRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE ${WIVRN_FEATURE_STEAMVR_LIGHTHOUSE})
set(XRT_BUILD_DRIVER_TWRAP              OFF)
set(XRT_BUILD_DRIVER_VF                 OFF)
set(XRT_BUILD_DRIVER_VIVE               OFF)
set(XRT_BUILD_DRIVER_WMR                OFF)
set(XRT_BUILD_DRIVER_XREAL_AIR          OFF)
set(XRT_BUILD_SAMPLES                   OFF)

set(XRT_FEATURE_COMPOSITOR_NULL                   OFF)
set(XRT_FEATURE_DEBUG_GUI                         ${WIVRN_FEATURE_DEBUG_GUI})
set(XRT_FEATURE_SERVICE                           ON)
set(XRT_FEATURE_SERVICE_SYSTEMD                   OFF)
set(XRT_FEATURE_SLAM                              OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN                    OFF)
set(XRT_FEATURE_WINDOW_PEEK                       OFF)
set(XRT_FEATURE_OPENXR_ACTIVE_ACTION_SET_PRIORITY ON)
set(XRT_FEATURE_RENDERDOC                         ${WIVRN_FEATURE_RENDERDOC})

set(XRT_FEATURE_OPENXR_FACE_TRACKING2_FB         ON)
set(XRT_FEATURE_OPENXR_FACIAL_TRACKING_HTC       ON)
set(XRT_FEATURE_OPENXR_HAND_TRACKING_EXT         ON)
set(XRT_FEATURE_OPENXR_INTERACTION_EXT_HAND      ON)
set(XRT_FEATURE_OPENXR_INTERACTION_EXT_PALM_POSE ON)
set(XRT_FEATURE_OPENXR_INTERACTION_BYTEDANCE     ON)
set(XRT_FEATURE_OPENXR_INTERACTION_VIVE_FOCUS3   ON)
set(XRT_FEATURE_OPENXR_INTERACTION_TOUCH_PRO     ON)
set(XRT_FEATURE_OPENXR_INTERACTION_TOUCH_PLUS    ON)
set(XRT_FEATURE_OPENXR_LAYER_COLOR_SCALE_BIAS    ON)

set(XRT_HAVE_BLUETOOTH               OFF)
set(XRT_HAVE_GST                     OFF)

set(XRT_HAVE_JPEG                    OFF)
if (NOT WIVRN_FEATURE_DEBUG_GUI)
	set(XRT_HAVE_SDL2                OFF)
endif()
set(XRT_HAVE_OPENCV                  OFF)
set(XRT_HAVE_DBUS                    OFF)
set(XRT_HAVE_LIBUDEV                 OFF)
set(XRT_HAVE_ONNXRUNTIME             OFF)

set(XRT_HAVE_SYSTEMD ${WIVRN_USE_SYSTEMD})
set(XRT_INSTALL_SYSTEMD_UNIT_FILES   OFF)

set(BUILD_DOC                        OFF)
set(BUILD_TESTING                    OFF)

set(XRT_HAVE_SYSTEM_CJSON            OFF)

set(XRT_IPC_MSG_SOCK_FILENAME     "wivrn/comp_ipc" CACHE STRING "Service socket filename")
set(XRT_IPC_SERVICE_PID_FILENAME  "wivrn.pid"      CACHE STRING "Service pidfile filename")
set(XRT_OXR_RUNTIME_SUFFIX        "wivrn"          CACHE STRING "OpenXR client library suffix")

FetchContent_MakeAvailable(monado)

if (WIVRN_BUILD_SERVER)
	add_executable(wivrn-server
			accept_connection.cpp
			active_runtime.cpp
			avahi_publisher.cpp
			hostname.cpp
			main.cpp
			sleep_inhibitor.cpp
			start_application.cpp
			ipc_server_cb.cpp
			target_instance_wivrn.cpp
			wivrn_ipc.cpp

			audio/audio_setup.cpp

			encoder/encoder_settings.cpp
			encoder/video_encoder.cpp
			encoder/video_encoder_raw.cpp

			driver/app_pacer.cpp
			driver/clock_offset.cpp
			driver/configuration.cpp
			driver/wivrn_hmd.cpp
			driver/wivrn_pacer.cpp
			driver/wivrn_comp_target.cpp
			driver/wivrn_controller.cpp
			driver/wivrn_hand_interaction.cpp
			driver/wivrn_eye_tracker.cpp
			driver/wivrn_fb_face2_tracker.cpp
			driver/wivrn_htc_face_tracker.cpp
			driver/wivrn_generic_tracker.cpp
			driver/wivrn_foveation.cpp
			driver/pose_list.cpp
			driver/view_list.cpp
			driver/hand_joints_list.cpp
			driver/wivrn_session.cpp
			driver/wivrn_connection.cpp
			driver/xrt_cast.cpp

			utils/wivrn_vk_bundle.cpp
		)
	target_compile_features(wivrn-server PRIVATE cxx_std_20)
	target_compile_definitions(wivrn-server PRIVATE VULKAN_HPP_NO_CONSTRUCTORS)

	target_include_directories(wivrn-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/compositor/)
	target_include_directories(wivrn-server PRIVATE .)

	target_link_libraries(wivrn-server PRIVATE CLI11::CLI11 xrt-external-renderdoc)

	if (WIVRN_FEATURE_STEAMVR_LIGHTHOUSE)
		target_include_directories(wivrn-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/drivers/steamvr_lh/)
		target_link_libraries(wivrn-server PRIVATE drv_steamvr_lh)
	endif()

	if (WIVRN_FEATURE_SOLARXR)
		target_include_directories(wivrn-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/drivers/solarxr/)
		target_link_libraries(wivrn-server PRIVATE drv_solarxr)
	endif()

	if(WIVRN_USE_NVENC)
		target_sources(
			wivrn-server
			PRIVATE encoder/video_encoder_nvenc.cpp
				encoder/video_encoder_nvenc_shared_state.cpp
			)
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

	if(WIVRN_USE_VULKAN_ENCODE)
		target_sources(wivrn-server PRIVATE
			encoder/video_encoder_vulkan.cpp
			encoder/video_encoder_vulkan_h264.cpp
			#encoder/video_encoder_vulkan_h265.cpp
			)
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

	# Generate dbus interface code as a separate target to avoid warnings in generated code
	add_custom_command(OUTPUT wivrn_server_dbus.c wivrn_server_dbus.h
			   COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml --interface-prefix io.github.wivrn --generate-c-code wivrn_server_dbus --c-namespace Wivrn
			   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml)
	add_custom_target(wivrn-server-dbus-codegen ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wivrn_server_dbus.c ${CMAKE_CURRENT_BINARY_DIR}/wivrn_server_dbus.h)
	add_library(wivrn-server-dbus STATIC
		${CMAKE_CURRENT_BINARY_DIR}/wivrn_server_dbus.c
		${CMAKE_CURRENT_BINARY_DIR}/wivrn_server_dbus.h
		)
	target_include_directories(wivrn-server-dbus PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
	add_dependencies(wivrn-server-dbus wivrn-server-dbus-codegen)
	target_link_libraries(wivrn-server-dbus PUBLIC PkgConfig::glib2)
	target_link_libraries(wivrn-server PUBLIC wivrn-server-dbus)
	target_link_libraries(wivrn-server PUBLIC PkgConfig::libnotify)

	if(WIVRN_USE_SYSTEMD)
		add_custom_command(OUTPUT systemd_manager.c systemd_manager.h
				   COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml --interface-prefix org.freedesktop.systemd1 --generate-c-code systemd_manager --c-namespace systemd
				   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml )
		add_custom_command(OUTPUT systemd_unit.c systemd_unit.h
			COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Unit.xml --interface-prefix org.freedesktop.systemd1 --generate-c-code systemd_unit --c-namespace unit
				   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml )
		target_sources(wivrn-server-dbus PRIVATE
			${CMAKE_CURRENT_BINARY_DIR}/systemd_manager.c ${CMAKE_CURRENT_BINARY_DIR}/systemd_manager.h
			${CMAKE_CURRENT_BINARY_DIR}/systemd_unit.c ${CMAKE_CURRENT_BINARY_DIR}/systemd_unit.h)
		target_sources(
			wivrn-server
			PRIVATE start_systemd_unit.cpp
			)
	endif()

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
			wivrn-common-server
			wivrn-external
			nlohmann_json::nlohmann_json
		)

	target_compile_definitions(wivrn-server PRIVATE JSON_DIAGNOSTICS=1)

	configure_file(dist/wivrn.service.in wivrn.service)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wivrn.service
		DESTINATION lib/systemd/user/)

	install(FILES dist/firewalld-wivrn.xml
		RENAME wivrn.xml
		DESTINATION lib/firewalld/services)

	install(TARGETS wivrn-server)
endif()

if (WIVRN_BUILD_SERVER_LIBRARY)
	set_target_properties(${RUNTIME_TARGET} PROPERTIES EXCLUDE_FROM_ALL FALSE)
	set_target_properties(monado PROPERTIES EXCLUDE_FROM_ALL FALSE)

	set(_extra_args)
	if (WIVRN_OPENXR_MANIFEST_TYPE STREQUAL "absolute")
		set(_extra_args ABSOLUTE_RUNTIME_PATH)
	elseif (WIVRN_OPENXR_MANIFEST_TYPE STREQUAL "relative")
		set(_extra_args RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${CMAKE_INSTALL_LIBDIR}/wivrn)
	endif()

	set_target_properties(monado PROPERTIES OUTPUT_NAME "monado_wivrn")
	# Workaround as generate_openxr_runtime_manifest_at_install uses target name as library path
	add_library(monado_wivrn ALIAS monado)

	generate_openxr_runtime_manifest_at_install(
		${_extra_args}
		RUNTIME_TARGET ${RUNTIME_TARGET}
		DESTINATION share/openxr/1
		RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_LIBDIR}/wivrn
		LIBMONADO monado_wivrn
		)

	install(TARGETS ${RUNTIME_TARGET} monado
		LIBRARY
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/wivrn
		COMPONENT Runtime)
endif()
