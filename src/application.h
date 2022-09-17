/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef XR_USE_PLATFORM_ANDROID
#include <android_native_app_glue.h>
#endif

#include "spdlog/spdlog.h"
#include "vk/vk.h"
#include "xr/xr.h"
#include <array>
#include <mutex>
#include <unordered_set>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

class scene;

struct application_info
{
	std::string name = "Unnamed application";
	int version = VK_MAKE_VERSION(1, 0, 0);
	XrFormFactor formfactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrViewConfigurationType viewconfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XrVersion min_vulkan_version = XR_MAKE_VERSION(1, 1, 0);

#ifdef XR_USE_PLATFORM_ANDROID
	android_app * native_app;
#endif
};

class application;

#ifdef XR_USE_PLATFORM_ANDROID
class jni_thread
{
	JNIEnv * env = nullptr;
	JavaVM * vm = nullptr;

public:
	explicit jni_thread(application & app);
	jni_thread();
	~jni_thread();

	JNIEnv * get_JNIEnv()
	{
		return env;
	}
};
#else
class [[maybe_unused]] jni_thread
{
public:
	explicit jni_thread(application & app) {}
	jni_thread() = default;
	~jni_thread() = default;
};
#endif

class application
{
	friend class scene;
	friend class jni_thread;

	application_info app_info;
#ifdef XR_USE_PLATFORM_ANDROID
	ANativeWindow * native_window = nullptr;
	bool resumed = false;
#endif

	static inline const char engine_name[] = "No engine";
	static inline const int engine_version = VK_MAKE_VERSION(1, 0, 0);

	static inline const std::array<VkFormat, 2> supported_formats = {VK_FORMAT_R8G8B8A8_SRGB,
	                                                                 VK_FORMAT_B8G8R8A8_SRGB};

	void initialize_vulkan();

	void log_views();

	void initialize();
	void cleanup();

	void poll_events();

	void session_state_changed(XrSessionState new_state, XrTime timestamp);
	void interaction_profile_changed();

	static application * instance_;

	jni_thread jni;

	// OpenXR stuff
	xr::instance xr_instance;
	xr::system xr_system_id;
	xr::session xr_session;
	std::vector<xr::swapchain> xr_swapchains;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;

	xr::space world_space;
	xr::space view_space;
	xr::actionset xr_actionset;
	std::vector<std::tuple<XrAction, XrActionType, std::string>> actions;
	std::vector<xr::space> action_spaces;

	// Vulkan stuff
	VkInstance vk_instance{};
	VkPhysicalDevice vk_physical_device{};
	VkDevice vk_device{};
	uint32_t vk_queue_family_index;
	VkQueue vk_queue{};

	vk::renderpass vk_renderpass;
	vk::pipeline vk_pipeline;
	vk::command_pool vk_cmdpool;

	VkFormat swapchain_format;

	static inline const std::pair<const char *, XrActionType> oculus_touch[] = {
	        {"/user/hand/left/output/haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},
	        {"/user/hand/right/output/haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},

	        {"/user/hand/left/input/grip/pose", XR_ACTION_TYPE_POSE_INPUT},
	        {"/user/hand/left/input/aim/pose", XR_ACTION_TYPE_POSE_INPUT},

	        {"/user/hand/right/input/grip/pose", XR_ACTION_TYPE_POSE_INPUT},
	        {"/user/hand/right/input/aim/pose", XR_ACTION_TYPE_POSE_INPUT},

	        {"/user/hand/left/input/x/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/x/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/y/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/y/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/menu/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/squeeze/value", XR_ACTION_TYPE_FLOAT_INPUT},
	        {"/user/hand/left/input/trigger/value", XR_ACTION_TYPE_FLOAT_INPUT},
	        {"/user/hand/left/input/trigger/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
	        {"/user/hand/left/input/thumbstick/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/thumbstick/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/left/input/thumbrest/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},

	        {"/user/hand/right/input/a/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/a/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/b/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/b/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/system/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/squeeze/value", XR_ACTION_TYPE_FLOAT_INPUT},
	        {"/user/hand/right/input/trigger/value", XR_ACTION_TYPE_FLOAT_INPUT},
	        {"/user/hand/right/input/trigger/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
	        {"/user/hand/right/input/thumbstick/click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/thumbstick/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        {"/user/hand/right/input/thumbrest/touch", XR_ACTION_TYPE_BOOLEAN_INPUT},
	};

	bool session_running = false;
	bool session_focused = false;
	bool session_visible = false;
	bool debug_extensions_found = false;
	std::atomic<bool> exit_requested = false;

	std::mutex scene_stack_lock;
	std::vector<std::shared_ptr<scene>> scene_stack;
	std::shared_ptr<scene> last_scene;

	void loop();

	static VkBool32 vulkan_debug_report_callback(VkDebugReportFlagsEXT flags,
	                                             VkDebugReportObjectTypeEXT objectType,
	                                             uint64_t object,
	                                             size_t location,
	                                             int32_t messageCode,
	                                             const char * pLayerPrefix,
	                                             const char * pMessage,
	                                             void * pUserData);
	std::unordered_set<uint64_t> debug_report_ignored_objects;
	std::unordered_map<uint64_t, std::string> debug_report_object_name;

public:
	application(application_info info);

	application(const application &) = delete;
	~application();

	static bool is_session_running()
	{
		return instance_->session_running;
	}

	static bool is_focused()
	{
		return instance_->session_focused;
	}

	static bool is_visible()
	{
		return instance_->session_visible;
	}

	static bool is_exit_requested()
	{
		return instance_->exit_requested;
	};

	static void poll_actions();
	static bool read_action(XrAction action, float & value);
	static bool read_action(XrAction action, bool & value);
	static bool read_action(XrAction action, XrVector2f & value);
	static void haptic_start(XrAction action, XrPath subpath, int64_t duration, float frequency, float amplitude);
	static void haptic_stop(XrAction action, XrPath subpath);

	static const std::vector<std::tuple<XrAction, XrActionType, std::string>> & inputs()
	{
		return instance_->actions;
	}
	static std::pair<XrAction, XrActionType> get_action(const std::string & name);

	static XrPath string_to_path(const std::string & s)
	{
		return instance_->xr_instance.string_to_path(s);
	}

	static std::string path_to_string(XrPath p)
	{
		return instance_->xr_instance.path_to_string(p);
	}

	void run();

	static void push_scene(std::shared_ptr<scene>);

	template <typename T, typename... Args>
	static void push_scene(Args &&... args)
	{
		std::unique_lock _{instance_->scene_stack_lock};

		instance_->scene_stack.push_back(std::make_shared<T>(std::forward<Args>(args)...));
	}

	static void pop_scene();

	static std::shared_ptr<scene> current_scene();

	static application & instance()
	{
		assert(instance_ != nullptr);
		return *instance_;
	}

	template <typename T>
	static T get_vulkan_proc(const char * proc_name)
	{
		auto proc = vkGetInstanceProcAddr(instance_->vk_instance, proc_name);
		if (!proc)
			throw std::runtime_error(std::string("Cannot find Vulkan function ") + proc_name);
		return (T)proc;
	}

	static XrSpace view()
	{
		return instance_->view_space;
	};
	static XrSpace left_grip()
	{
		return instance_->action_spaces[0];
	};
	static XrSpace left_aim()
	{
		return instance_->action_spaces[1];
	};
	static XrSpace right_grip()
	{
		return instance_->action_spaces[2];
	};
	static XrSpace right_aim()
	{
		return instance_->action_spaces[3];
	};

	static void ignore_debug_reports_for(void * object)
	{
#ifndef NDEBUG
		instance_->debug_report_ignored_objects.emplace((uint64_t)object);
#endif
	}

	static void unignore_debug_reports_for(void * object)
	{
#ifndef NDEBUG
		instance_->debug_report_ignored_objects.erase((uint64_t)object);
#endif
	}

	static void set_debug_reports_name(void * object, std::string name)
	{
#ifndef NDEBUG
		instance_->debug_report_object_name[(uint64_t)object] = std::move(name);
#endif
	}

	static XrTime now()
	{
		return instance_->xr_instance.now();
	}

	static uint32_t queue_family_index()
	{
		return instance_->vk_queue_family_index;
	}
};
