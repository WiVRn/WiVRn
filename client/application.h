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

#ifdef __ANDROID__
#include <android_native_app_glue.h>
#endif

#include "xr/xr.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>
#include "vk_mem_alloc.h"
#include "utils/singleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

class scene;

struct application_info
{
	std::string name = "Unnamed application";
	int version = VK_MAKE_VERSION(1, 0, 0);
	XrFormFactor formfactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrViewConfigurationType viewconfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XrVersion min_vulkan_version = XR_MAKE_VERSION(1, 1, 0);

#ifdef __ANDROID__
	android_app * native_app;
#endif
};

class application : public singleton<application>
{
	friend class scene;

	application_info app_info;
#ifdef __ANDROID__
	ANativeWindow * native_window = nullptr;
	bool resumed = false;
#endif

	static inline const char engine_name[] = "No engine";
	static inline const int engine_version = VK_MAKE_VERSION(1, 0, 0);

	void initialize_vulkan();

	void log_views();

	void initialize();
	void cleanup();

	void poll_events();

	void session_state_changed(XrSessionState new_state, XrTime timestamp);
	void interaction_profile_changed();

	// Vulkan stuff
	vk::raii::Context vk_context;
	vk::raii::Instance vk_instance = nullptr;
	vk::raii::PhysicalDevice vk_physical_device = nullptr;
	vk::raii::Device vk_device = nullptr;
	uint32_t vk_queue_family_index;
	vk::raii::Queue vk_queue = nullptr;
	vk::raii::CommandPool vk_cmdpool = nullptr;
	vk::raii::PipelineCache pipeline_cache = nullptr;

	// OpenXR stuff
	void initialize_actions();

	xr::instance xr_instance;
	xr::system xr_system_id;
	xr::session xr_session;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;

	xr::space world_space;
	xr::space view_space;
	xr::actionset xr_actionset;
	std::vector<std::tuple<XrAction, XrActionType, std::string>> actions;
	xr::space left_grip_space;
	xr::space left_aim_space;
	xr::space right_grip_space;
	xr::space right_aim_space;

	// Vulkan memory allocator stuff
	VmaAllocator allocator;


	bool session_running = false;
	bool session_focused = false;
	bool session_visible = false;
	bool debug_extensions_found = false;
	std::vector<std::string> xr_extensions;
	std::atomic<bool> exit_requested = false;
	std::filesystem::path config_path;
	std::filesystem::path cache_path;

	std::string server_address;

	std::mutex scene_stack_lock;
	std::vector<std::shared_ptr<scene>> scene_stack;
	std::weak_ptr<scene> last_scene;

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


#ifndef NDEBUG
	vk::raii::DebugReportCallbackEXT debug_report_callback = nullptr;
#endif

public:
	using singleton<application>::instance;
	application(application_info info);

	application(const application &) = delete;
	~application();
#ifdef __ANDROID__
	void setup_jni();

	void set_wifi_locks(bool enabled);

	static AAssetManager* asset_manager()
	{
		return instance().app_info.native_app->activity->assetManager;
	}
#endif

	static bool is_session_running()
	{
		return instance().session_running;
	}

	static bool is_focused()
	{
		return instance().session_focused;
	}

	static bool is_visible()
	{
		return instance().session_visible;
	}

	static bool is_exit_requested()
	{
		return instance().exit_requested;
	};

	static void poll_actions();
	static std::optional<std::pair<XrTime, float>> read_action_float(XrAction action);
	static std::optional<std::pair<XrTime, bool>> read_action_bool(XrAction action);
	static std::optional<std::pair<XrTime, XrVector2f>> read_action_vec2(XrAction action);
	static void haptic_start(XrAction action, XrPath subpath, int64_t duration, float frequency, float amplitude);
	static void haptic_stop(XrAction action, XrPath subpath);

	static const std::vector<std::tuple<XrAction, XrActionType, std::string>> & inputs()
	{
		return instance().actions;
	}
	static std::pair<XrAction, XrActionType> get_action(const std::string & name);

	static std::optional<std::pair<glm::vec3, glm::quat>> locate_controller(XrSpace space, XrSpace reference, XrTime time)
	{
		XrSpaceLocation location{.type = XR_TYPE_SPACE_LOCATION};

		assert(space != XR_NULL_HANDLE);
		assert(reference != XR_NULL_HANDLE);

		xrLocateSpace(space, reference, time, &location);

		auto req_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		if ((location.locationFlags & req_flags) != req_flags)
			return {};

		glm::vec3 position{
			location.pose.position.x,
			location.pose.position.y,
			location.pose.position.z,
		};

		glm::quat orientation{
			location.pose.orientation.w,
			location.pose.orientation.x,
			location.pose.orientation.y,
			location.pose.orientation.z,
		};

		return std::make_pair(position, orientation);
	}

	static XrPath string_to_path(const std::string & s)
	{
		return instance().xr_instance.string_to_path(s);
	}

	static std::string path_to_string(XrPath p)
	{
		return instance().xr_instance.path_to_string(p);
	}

	void run();

	static void push_scene(std::shared_ptr<scene>);

	template <typename T, typename... Args>
	static void push_scene(Args &&... args)
	{
		std::unique_lock _{instance().scene_stack_lock};

		instance().scene_stack.push_back(std::make_shared<T>(std::forward<Args>(args)...));
	}

	static void pop_scene();

	static std::shared_ptr<scene> current_scene();

	template <typename T>
	static T get_vulkan_proc(const char * proc_name)
	{
		auto proc = instance().vk_instance.getProcAddr(proc_name);
		if (!proc)
			throw std::runtime_error(std::string("Cannot find Vulkan function ") + proc_name);
		return (T)proc;
	}

	static XrSpace view()
	{
		return instance().view_space;
	};
	static XrSpace left_grip()
	{
		return instance().left_grip_space;
	};
	static XrSpace left_aim()
	{
		return instance().left_aim_space;
	};
	static XrSpace right_grip()
	{
		return instance().right_grip_space;
	};
	static XrSpace right_aim()
	{
		return instance().right_aim_space;
	};

	static void ignore_debug_reports_for(void * object)
	{
#ifndef NDEBUG
		instance().debug_report_ignored_objects.emplace((uint64_t)object);
#endif
	}

	static void unignore_debug_reports_for(void * object)
	{
#ifndef NDEBUG
		instance().debug_report_ignored_objects.erase((uint64_t)object);
#endif
	}

	template<typename T>
	static void set_debug_reports_name(const T& object, std::string name)
	{
// #ifndef NDEBUG
		// if (instance().debug_utils_found)
			const vk::DebugUtilsObjectNameInfoEXT name_info {
				.objectType = T::objectType,
				.objectHandle = (uint64_t)(typename T::NativeType)object,
				.pObjectName = name.c_str(),
			};

			instance().vk_device.setDebugUtilsObjectNameEXT(name_info);

		// printf("set_debug_reports_name %p, %s\n", object, name.c_str());
		// instance().debug_report_object_name[(uint64_t)object] = std::move(name);
// #endif
	}

	static XrTime now()
	{
		return instance().xr_instance.now();
	}

	static uint32_t queue_family_index()
	{
		return instance().vk_queue_family_index;
	}

	static vk::raii::Queue& get_queue()
	{
			return instance().vk_queue;
	}

	const std::string& get_server_address() const
	{
		return server_address;
	}

	static VmaAllocator get_allocator()
	{
		return instance().allocator;
	}

	static vk::raii::PhysicalDevice& get_physical_device()
	{
		return instance().vk_physical_device;
	}

	static vk::raii::Device& get_device()
	{
		return instance().vk_device;
	}

	static vk::raii::Instance& get_vulkan_instance()
	{
			return instance().vk_instance;
	}

	static vk::raii::PipelineCache& get_pipeline_cache()
	{
			return instance().pipeline_cache;
	}

	static const std::filesystem::path& get_config_path()
	{
		return instance().config_path;
	}


	static const std::filesystem::path& get_cache_path()
	{
		return instance().cache_path;
	}
};
