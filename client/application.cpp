/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "application.h"
#include "hardware.h"
#include "magic_enum.hpp"
#include "openxr/openxr.h"
#include "scene.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "utils/files.h"
#include "utils/named_thread.h"
#include "xr/actionset.h"
#include "xr/xr.h"
#include <algorithm>
#include "utils/contains.h"
#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <string>
#include <exception>
#include <thread>
#include <vector>
#include <vk_mem_alloc.h>
#include "vk/check.h"
#include "xr/check.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <openxr/openxr_platform.h>

#ifndef NDEBUG
#include "utils/backtrace.h"
#endif

#ifdef __ANDROID__
#include <android/native_activity.h>
#include <sys/system_properties.h>

#include "jnipp.h"
#else
#include <signal.h>
#endif

using namespace std::chrono_literals;


struct interaction_profile
{
	std::string profile_name;
	std::vector<std::string> required_extensions;
	std::vector<std::string> input_sources;
	bool available;
};

static std::vector<interaction_profile> interaction_profiles{
        interaction_profile{
                "/interaction_profiles/khr/simple_controller",
                {},
                {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/select/click",

                        "/user/hand/right/input/menu/click",
                        "/user/hand/right/input/select/click",

                }},
        interaction_profile{
                "/interaction_profiles/oculus/touch_controller",
                {},
                {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbrest/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                }},
        interaction_profile{
                "/interaction_profiles/bytedance/pico_neo3_controller",
                { "XR_BD_controller_interaction" },
                {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/system/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/menu/click",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",

                }},
        interaction_profile{
                "/interaction_profiles/bytedance/pico4_controller",
                { "XR_BD_controller_interaction" },
                {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                }},
        interaction_profile{
                "/interaction_profiles/htc/vive_focus3_controller",
                { "XR_HTC_vive_focus3_controller_interaction" },
                {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/touch",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/click",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbrest/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/touch",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/click",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                }},
};

static const std::pair<std::string_view, XrActionType> action_suffixes[] =
        {
		// clang-format off
		// From OpenXR spec 1.0.33, §6.3.2 Input subpaths

		// Standard components
		{"/click",      XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/touch",      XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/force",      XR_ACTION_TYPE_FLOAT_INPUT},
		{"/value",      XR_ACTION_TYPE_FLOAT_INPUT},
		{"/x",          XR_ACTION_TYPE_FLOAT_INPUT},
		{"/y",          XR_ACTION_TYPE_FLOAT_INPUT},
		{"/twist",      XR_ACTION_TYPE_FLOAT_INPUT},
		{"/pose",       XR_ACTION_TYPE_POSE_INPUT},

		// Standard 2D identifier, can be used without the /x and /y cmoponents
                {"/trackpad",   XR_ACTION_TYPE_VECTOR2F_INPUT},
                {"/thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
                {"/joystick",   XR_ACTION_TYPE_VECTOR2F_INPUT},
                {"/trackball",  XR_ACTION_TYPE_VECTOR2F_INPUT},

		// Output paths
                {"/haptic",     XR_ACTION_TYPE_VIBRATION_OUTPUT},
		// clang-format on
};

static XrActionType guess_action_type(const std::string & name)
{
	for(const auto & [suffix, type]: action_suffixes)
	{
		if (name.ends_with(suffix))
			return type;
	}

	return XR_ACTION_TYPE_FLOAT_INPUT;
}

#ifndef __ANDROID__
static std::filesystem::path get_config_base_dir()
{
	const char * xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	if (xdg_config_home)
		return xdg_config_home;
	const char * home = std::getenv("HOME");
	if (home)
		return std::filesystem::path(home) / ".config";
	return ".";
}

static std::filesystem::path get_cache_base_dir()
{
	const char * xdg_cache_home = std::getenv("XDG_CACHE_HOME");
	if (xdg_cache_home)
		return xdg_cache_home;
	const char * home = std::getenv("HOME");
	if (home)
		return std::filesystem::path(home) / ".cache";
	return ".";
}
#endif

VkBool32 application::vulkan_debug_report_callback(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType,
        uint64_t object,
        size_t location,
        int32_t messageCode,
        const char * pLayerPrefix,
        const char * pMessage,
        void * pUserData)
{
	if (instance().debug_report_ignored_objects.contains(object))
		return VK_FALSE;

	spdlog::level::level_enum level = spdlog::level::info;

	if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
	{
		level = spdlog::level::info;
	}
	else if (flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT))
	{
		level = spdlog::level::warn;
	}
	else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		level = spdlog::level::err;
	}
	else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		level = spdlog::level::debug;
	}

	// for(const std::string& s: utils::split(pMessage, "|"))
	// spdlog::log(level, s);
	spdlog::log(level, pMessage);

	auto it = instance().debug_report_object_name.find(object);
	if (it != instance().debug_report_object_name.end())
	{
		spdlog::log(level, "{:#016x}: {}", object, it->second);
	}

#ifndef NDEBUG
	if (level >= spdlog::level::warn)
	{
		bool validation_layer_found = false;
		for (auto & i: utils::backtrace(20))
		{
			if (i.library == "libVkLayer_khronos_validation.so")
				validation_layer_found = true;

			if (validation_layer_found && i.library != "libVkLayer_khronos_validation.so")
				spdlog::log(level, "{:#016x}: {} + {:#x}", i.pc, i.library, i.pc - i.library_base);
		}
	}

	if (level >= spdlog::level::err)
		abort();
#endif

	return VK_FALSE;
}

void application::initialize_vulkan()
{
	auto graphics_requirements = xr_system_id.graphics_requirements();
	XrVersion vulkan_version = std::max(app_info.min_vulkan_version, graphics_requirements.minApiVersionSupported);
	spdlog::info("OpenXR runtime wants Vulkan {}", xr::to_string(graphics_requirements.minApiVersionSupported));
	spdlog::info("Requesting Vulkan {}", xr::to_string(vulkan_version));

	std::vector<const char *> layers;

	spdlog::info("Available Vulkan layers:");
	[[maybe_unused]] bool validation_layer_found = false;

	for(vk::LayerProperties& i: vk_context.enumerateInstanceLayerProperties())
	{
		spdlog::info("    {}", i.layerName);
		if (!strcmp(i.layerName, "VK_LAYER_KHRONOS_validation"))
		{
			validation_layer_found = true;
		}
	}
#ifndef NDEBUG
	if (validation_layer_found)
	{
		spdlog::info("Using Vulkan validation layer");
		layers.push_back("VK_LAYER_KHRONOS_validation");
	}
#endif

	std::vector<const char *> instance_extensions{};
	std::vector<const char *> device_extensions{};

#ifndef NDEBUG
	bool debug_report_found = false;
	bool debug_utils_found = false;
#endif
	spdlog::info("Available Vulkan instance extensions:");
	for(vk::ExtensionProperties& i: vk_context.enumerateInstanceExtensionProperties(nullptr))
	{
		spdlog::info("    {}", i.extensionName);

#ifndef NDEBUG
		if (!strcmp(i.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
			debug_report_found = true;

		if (!strcmp(i.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			debug_utils_found = true;
#endif
	}

#ifndef NDEBUG
	if (debug_utils_found && debug_report_found)
	{
		// debug_extensions_found = true;
		// spdlog::info("Using debug extensions");
		instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		// instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
#endif

#ifdef __ANDROID__
	device_extensions.push_back(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	device_extensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#endif

	vk::ApplicationInfo application_info{
		.pApplicationName = app_info.name.c_str(),
		.applicationVersion = (uint32_t)app_info.version,
		.pEngineName = engine_name,
		.engineVersion = engine_version,
		.apiVersion = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(vulkan_version), XR_VERSION_MINOR(vulkan_version), 0),
	};

	vk::InstanceCreateInfo instance_create_info{
		.pApplicationInfo = &application_info,
	};
	instance_create_info.setPEnabledLayerNames(layers);
	instance_create_info.setPEnabledExtensionNames(instance_extensions);

	XrVulkanInstanceCreateInfoKHR create_info{
		.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
		.systemId = xr_system_id,
		.createFlags = 0,
		.pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vulkanCreateInfo = &(VkInstanceCreateInfo&)instance_create_info,
		.vulkanAllocator = nullptr,
	};

	auto xrCreateVulkanInstanceKHR =
	        xr_instance.get_proc<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");

	VkResult vresult;
	VkInstance tmp;
	XrResult xresult = xrCreateVulkanInstanceKHR(xr_instance, &create_info, &tmp, &vresult);
	CHECK_VK(vresult, "xrCreateVulkanInstanceKHR");
	CHECK_XR(xresult, "xrCreateVulkanInstanceKHR");
	vk_instance = vk::raii::Instance(vk_context, tmp);

#ifndef NDEBUG
	if (debug_report_found)
	{
		vk::DebugReportCallbackCreateInfoEXT debug_report_info{
		        .flags = vk::DebugReportFlagBitsEXT::eInformation |
				vk::DebugReportFlagBitsEXT::eWarning|
				vk::DebugReportFlagBitsEXT::ePerformanceWarning |
				vk::DebugReportFlagBitsEXT::eError |
				vk::DebugReportFlagBitsEXT::eDebug,
		        .pfnCallback = vulkan_debug_report_callback,
		};
		debug_report_callback = vk::raii::DebugReportCallbackEXT(vk_instance, debug_report_info);
	}
#endif

	vk_physical_device = xr_system_id.physical_device(vk_instance);

	spdlog::info("Available Vulkan device extensions:");
	for(vk::ExtensionProperties & i: vk_physical_device.enumerateDeviceExtensionProperties())
	{
		spdlog::info("    {}", i.extensionName);
	}

	vk::PhysicalDeviceProperties prop = vk_physical_device.getProperties();
	spdlog::info("Initializing Vulkan with device {}", prop.deviceName);

	std::vector<vk::QueueFamilyProperties> queue_properties = vk_physical_device.getQueueFamilyProperties();

	vk_queue_family_index = -1;
	[[maybe_unused]] bool vk_queue_found = false;
	for (size_t i = 0; i < queue_properties.size(); i++)
	{
		if (queue_properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			vk_queue_found = true;
			vk_queue_family_index = i;
			break;
		}
	}
	assert(vk_queue_found);
	spdlog::info("Using queue family {}", vk_queue_family_index);

	float queuePriority = 0.0f;

	vk::DeviceQueueCreateInfo queueCreateInfo{
		.queueFamilyIndex = vk_queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = &queuePriority
	};

	vk::PhysicalDeviceFeatures device_features{
		// .samplerAnisotropy = true,
	};

	vk::StructureChain device_create_info{
		vk::DeviceCreateInfo{
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueCreateInfo,
			.enabledExtensionCount = (uint32_t)device_extensions.size(),
			.ppEnabledExtensionNames = device_extensions.data(),
			.pEnabledFeatures = &device_features,
		},
		vk::PhysicalDeviceSamplerYcbcrConversionFeaturesKHR{
			.samplerYcbcrConversion = VK_TRUE
		}
	};

	vk_device = xr_system_id.create_device(vk_physical_device, device_create_info.get());

	vk_queue = vk_device.getQueue(vk_queue_family_index, 0);

	vk::PipelineCacheCreateInfo pipeline_cache_info;
	std::vector<std::byte> pipeline_cache_bytes;

	try {
		// TODO Robust pipeline cache serialization
		// https://zeux.io/2019/07/17/serializing-pipeline-cache/
		pipeline_cache_bytes = utils::read_whole_file<std::byte>(cache_path / "pipeline_cache");

		pipeline_cache_info.setInitialData<std::byte>(pipeline_cache_bytes);
	} catch (...) {
	}

	pipeline_cache = vk::raii::PipelineCache(vk_device, pipeline_cache_info);

	allocator.emplace(VmaAllocatorCreateInfo{
		.physicalDevice = *vk_physical_device,
		.device = *vk_device,
		.instance = *vk_instance,
	});
}

void application::log_views()
{
	for (XrViewConfigurationType i: xr_system_id.view_configurations())
	{
		spdlog::info("View configuration {}", xr::to_string(i));
		XrViewConfigurationProperties p = xr_system_id.view_configuration_properties(i);

		spdlog::info("    fovMutable: {}", p.fovMutable ? "true" : "false");

		int n = 0;
		for (const XrViewConfigurationView & j: xr_system_id.view_configuration_views(i))
		{
			spdlog::info("    View {}:", ++n);
			spdlog::info("        Recommended: {}x{}, {} sample(s)", j.recommendedImageRectWidth, j.recommendedImageRectHeight, j.recommendedSwapchainSampleCount);
			spdlog::info("        Maximum:     {}x{}, {} sample(s)", j.maxImageRectWidth, j.maxImageRectHeight, j.maxSwapchainSampleCount);
		}

		for (const XrEnvironmentBlendMode j: xr_system_id.environment_blend_modes(i))
		{
			spdlog::info("    Blend mode: {}", xr::to_string(j));
		}
	}
}

static std::string make_xr_name(std::string name)
{
	// Generate a name suitable for a path component (see OpenXR spec §6.2)
	for(char& c: name)
	{
		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			c = '_';
		else
			c = tolower(c);
	}

	size_t pos = name.find_first_of("abcdefghijklmnopqrstuvwxyz");

	return name.substr(pos);
}

void application::initialize_actions()
{
	spdlog::info("Initializing actions");

	// Build an action set with all possible input sources
	std::vector<XrActionSet> action_sets;
	xr_actionset = xr::actionset(xr_instance, "all_actions", "All actions");
	action_sets.push_back(xr_actionset);

	std::unordered_map<std::string, std::vector<XrActionSuggestedBinding>> suggested_bindings;

	// Build the list of all possible input sources, without duplicates,
	// checking which profiles are supported by the runtime
	std::vector<std::string> sources;
	for(auto & profile: interaction_profiles)
	{
		profile.available = utils::contains_all(xr_extensions, profile.required_extensions);

		if (!profile.available)
			continue;

		suggested_bindings.emplace(profile.profile_name, std::vector<XrActionSuggestedBinding>{});

		for(const std::string& source: profile.input_sources)
		{
			if (!utils::contains(sources, source))
				sources.push_back(source);
		}
	}

	// For each possible input source, create a XrAction and add it to the suggested binding
	std::unordered_map<std::string, XrAction> actions_by_name;

	for(std::string& name: sources)
	{
		std::string name_without_slashes = make_xr_name(name);

		XrActionType type = guess_action_type(name);

		auto a = xr_actionset.create_action(type, name_without_slashes);
		actions.emplace_back(a, type, name);
		actions_by_name.emplace(name, a);

		if (name == "/user/hand/left/input/grip/pose")
			left_grip_space = xr_session.create_action_space(a);
		else if (name == "/user/hand/left/input/aim/pose")
			left_aim_space = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/grip/pose")
			right_grip_space = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/aim/pose")
			right_aim_space = xr_session.create_action_space(a);
	}

	// Build an action set for each scene
	for(scene::meta* i: scene::scene_registry)
	{
		spdlog::info("  Scene \"{}\"", i->name);

		std::string actionset_name = make_xr_name(i->name);

		i->actionset = xr::actionset(xr_instance, actionset_name, i->name);
		action_sets.push_back(i->actionset);

		for(auto& [action_name, action_type] : i->actions)
		{
			spdlog::info("    Creating action {}", action_name);
			XrAction a = i->actionset.create_action(action_type, action_name);
			i->actions_by_name[action_name] = std::make_pair(a, action_type);

			if (action_type == XR_ACTION_TYPE_POSE_INPUT)
				i->spaces_by_name[action_name] = xr_session.create_action_space(a);
		}

		for(const scene::suggested_binding& j: i->bindings)
		{
			// Skip unsupported profiles
			if (!suggested_bindings.contains(j.profile_name))
				continue;

			spdlog::info("    Adding suggested bindings for {}", j.profile_name);
			std::vector<XrActionSuggestedBinding>& xr_bindings = suggested_bindings[j.profile_name];

			for(const scene::action_binding& k: j.paths)
			{
				spdlog::info("      {} => {}", k.action_name, k.input_source);
				XrAction a = i->actions_by_name[k.action_name].first;
				assert(a != XR_NULL_HANDLE);

				xr_bindings.push_back(XrActionSuggestedBinding{
					.action = a,
					.binding = string_to_path(k.input_source)
				});
			}
		}
	}

	// Suggest bindings for all supported controllers
	for (const auto & profile: interaction_profiles)
	{
		if (!profile.available)
		{
			spdlog::info("Skipping interaction profile {}", profile.profile_name);
			continue;
		}

		std::vector<XrActionSuggestedBinding>& xr_bindings = suggested_bindings[profile.profile_name];

		for (const auto & name: profile.input_sources)
		{
			xr_bindings.push_back({actions_by_name[name], string_to_path(name)});
		}

		spdlog::info("Suggesting bindings for interaction profile {}", profile.profile_name);
		try
		{
			xr_instance.suggest_bindings(profile.profile_name, xr_bindings);
		}
		catch(std::exception& e)
		{
			spdlog::warn("Caught {} while suggesting interaction profile", e.what());
		}
	}

	xr_session.attach_actionsets(action_sets);
}

void application::initialize()
{
	// LogLayersAndExtensions
	assert(!xr_instance);
	xr_extensions.clear();

	// Required extensions
	xr_extensions.push_back(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);

	// Optional extensions
	std::vector<std::string> opt_extensions;
	opt_extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
	opt_extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

	for(const auto& i: interaction_profiles)
	{
		for(const auto& j: i.required_extensions)
		{
			opt_extensions.push_back(j);
		}
	}

	for(const auto& i: xr::instance::extensions())
	{
		if (utils::contains(opt_extensions, i.extensionName))
			xr_extensions.push_back(i.extensionName);
	}

	std::vector<const char *> extensions;
	for(const auto& i: xr_extensions)
	{
		extensions.push_back(i.c_str());
	}

#ifdef __ANDROID__
	xr_instance =
	        xr::instance(app_info.name, app_info.native_app->activity->vm, app_info.native_app->activity->clazz, extensions);
#else
	xr_instance = xr::instance(app_info.name, extensions);
#endif

	spdlog::info("Created OpenXR instance, runtime {}, version {}", xr_instance.get_runtime_name(), xr_instance.get_runtime_version());

	xr_system_id = xr::system(xr_instance, app_info.formfactor);
	spdlog::info("Created OpenXR system for form factor {}", xr::to_string(app_info.formfactor));

	// Log system properties
	XrSystemProperties properties = xr_system_id.properties();
	spdlog::info("OpenXR sytem properties:");
	spdlog::info("    Vendor ID: {:#x}", properties.vendorId);
	spdlog::info("    System name: {}", properties.systemName);
	spdlog::info("    Graphics properties:");
	spdlog::info("        Maximum swapchain image size: {}x{}", properties.graphicsProperties.maxSwapchainImageWidth, properties.graphicsProperties.maxSwapchainImageWidth);
	spdlog::info("        Maximum layer count: {}", properties.graphicsProperties.maxLayerCount);
	spdlog::info("    Tracking properties:");
	spdlog::info("        Orientation tracking: {}", (bool)properties.trackingProperties.orientationTracking);
	spdlog::info("        Position tracking: {}", (bool)properties.trackingProperties.positionTracking);


	// Log view configurations and blend modes
	log_views();

	initialize_vulkan();

	xr_session = xr::session(xr_instance, xr_system_id, vk_instance, vk_physical_device, vk_device, vk_queue_family_index);

	auto spaces = xr_session.get_reference_spaces();
	spdlog::info("{} reference spaces", spaces.size());
	for (XrReferenceSpaceType i: spaces)
	{
		spdlog::info("    {}", xr::to_string(i));
	}

	view_space = xr_session.create_reference_space(XR_REFERENCE_SPACE_TYPE_VIEW);
	world_space = xr_session.create_reference_space(XR_REFERENCE_SPACE_TYPE_STAGE);
	// world_space = xr_session.create_reference_space(XR_REFERENCE_SPACE_TYPE_LOCAL);

	vk::CommandPoolCreateInfo cmdpool_create_info;
	cmdpool_create_info.queueFamilyIndex = vk_queue_family_index;
	cmdpool_create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	vk_cmdpool = vk::raii::CommandPool{vk_device, cmdpool_create_info};

	initialize_actions();

	interaction_profile_changed();
}



std::pair<XrAction, XrActionType> application::get_action(const std::string & requested_name)
{
	for (const auto & [action, type, name]: instance().actions)
	{
		if (name == requested_name)
			return {action, type};
	}

	return {};
}

application::application(application_info info) :
        app_info(std::move(info))
{
#ifdef __ANDROID__
	// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/types.html

	setup_jni();
	{
		jni::object<""> act(app_info.native_app->activity->clazz);
		auto app = act.call<jni::object<"android/app/Application">>("getApplication");
		auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

		// Get the intent, to handle wivrn://uri
		auto intent = act.call<jni::object<"android/content/Intent">>("getIntent");
		std::string data_string;
		if (auto data_string_jni = intent.call<jni::string>("getDataString"))
		{
			data_string = data_string_jni;
		}

		spdlog::info("dataString = {}", data_string);
		if (data_string.starts_with("wivrn://"))
		{
			server_address = data_string.substr(strlen("wivrn://"));
		}

		auto files_dir = ctx.call<jni::object<"java/io/File">>("getFilesDir");
		if (auto files_dir_path = files_dir.call<jni::string>("getAbsolutePath"))
		{
			config_path = files_dir_path;
			cache_path = files_dir_path;
		}
	}

	app_info.native_app->userData = this;
	app_info.native_app->onAppCmd = [](android_app * app, int32_t cmd) {
		switch (cmd)
		{
			// There is no APP_CMD_CREATE. The ANativeActivity creates the
			// application thread from onCreate(). The application thread
			// then calls android_main().
			case APP_CMD_START:
				break;
			case APP_CMD_RESUME:
				static_cast<application *>(app->userData)->resumed = true;
				break;
			case APP_CMD_PAUSE:
				static_cast<application *>(app->userData)->resumed = false;
				break;
			case APP_CMD_STOP:
				break;
			case APP_CMD_DESTROY:
				static_cast<application *>(app->userData)->native_window = nullptr;
				break;
			case APP_CMD_INIT_WINDOW:
				static_cast<application *>(app->userData)->native_window = app->window;
				break;
			case APP_CMD_TERM_WINDOW:
				static_cast<application *>(app->userData)->native_window = nullptr;
				break;
		}
	};

	// Initialize the loader for this platform
	PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
	if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)(&initializeLoader))))
	{
		XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid{
			.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
			.next = nullptr,
			.applicationVM = app_info.native_app->activity->vm,
			.applicationContext = app_info.native_app->activity->clazz,
		};
		initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInitInfoAndroid);
	}

#else
	config_path = get_config_base_dir() / "wivrn";
	cache_path = get_cache_base_dir() / "wivrn";
#endif

	std::filesystem::create_directories(config_path);
	std::filesystem::create_directories(cache_path);
	spdlog::info("Config path: {}", config_path.native());
	spdlog::info("Cache path: {}", cache_path.native());

	try
	{
		initialize();
	}
	catch (std::exception& e)
	{
		spdlog::error("Error during initialization: {}", e.what());
		cleanup();
		throw;
	}
}

#ifdef __ANDROID__
void application::setup_jni()
{
	jni::jni_thread::setup_thread(app_info.native_app->activity->vm);
}

void application::set_wifi_locks(bool enabled)
{
	jni::object<""> act(app_info.native_app->activity->clazz);

	jni::string lock_name("WiVRn");

	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");
	auto wifi_service_id = ctx.klass().field<jni::string>("WIFI_SERVICE");
	auto system_service = ctx.call<jni::object<"java/lang/Object">>("getSystemService", wifi_service_id);
	auto lock = system_service.call<jni::object<"android/net/wifi/WifiManager$MulticastLock">>("createMulticastLock", lock_name);
	lock.call<void>("setReferenceCounted", jni::Bool(false));
	lock.call<void>(enabled ? "acquire" : "release");
	if (lock.call<jni::Bool>("isHeld"))
	{
		spdlog::info("MulticastLock acquired");
	}
	else
	{
		spdlog::info("MulticastLock is not acquired");
	}

	auto wifi_lock = system_service.call<jni::object<"android/net/wifi/WifiManager$WifiLock">>("createWifiLock", jni::Int(3) /*WIFI_MODE_FULL_HIGH_PERF*/, lock_name);
	wifi_lock.call<void>("setReferenceCounted", jni::Bool(false));
	wifi_lock.call<void>(enabled ? "acquire" : "release");
	if (wifi_lock.call<jni::Bool>("isHeld"))
	{
		spdlog::info("WifiLock low latency acquired");
	}
	else
	{
		spdlog::info("WifiLock low latency is not acquired");
	}
}
#endif

void application::cleanup()
{
	// Remove all scenes before destroying the allocator
	scene_stack.clear();

	// Empty the meta objects while the OpenXR instance still exists
	for(auto i: scene::scene_registry)
	{
		xr::actionset tmp = std::move(i->actionset);
		i->actions_by_name.clear(); // Not strictly necessary
		i->spaces_by_name.clear();
	}

#ifdef __ANDROID__
	jni::jni_thread::detach();
#endif
}

application::~application()
{
	auto pipeline_cache_bytes = pipeline_cache.getData();
	utils::write_whole_file(cache_path / "pipeline_cache", pipeline_cache_bytes);

	cleanup();
}

void application::loop()
{
	poll_events();

	if (!is_session_running())
	{
		// Throttle loop since xrWaitFrame won't be called.
		std::this_thread::sleep_for(250ms);
	}
	else
	{

		auto scene = current_scene();
		if (scene)
		{
			poll_actions();
			if (auto tmp = last_scene.lock(); scene != tmp)
			{
				if (tmp)
					tmp->on_unfocused();

				scene->on_focused();

				last_scene = scene;
			}

			scene->render();
		}
		else
		{
			spdlog::info("Last scene popped: exiting");
			exit_requested = true;
		}
	}
}

#ifdef __ANDROID__
void application::run()
{
	auto application_thread = utils::named_thread("application_thread", [&]() {
		setup_jni();

		while (!is_exit_requested())
		{
			try
			{
				loop();
			}
			catch (std::exception & e)
			{
				spdlog::error("Caught exception in application_thread: \"{}\"", e.what());
				exit_requested = true;
			}
			catch (...)
			{
				spdlog::error("Caught unknown exception in application_thread");
				exit_requested = true;
			}
		}
	});

	// Read all pending events.
	while (!exit_requested)
	{
		int events;
		struct android_poll_source * source;

		// TODO signal with a file descriptor instead of a 100ms timeout
		while (ALooper_pollAll(100, nullptr, &events, (void **)&source) >= 0)
		{
			// Process this event.
			if (source != nullptr)
				source->process(app_info.native_app, source);
		}

		if (app_info.native_app->destroyRequested)
		{
			exit_requested = true;
		}
	}

	application_thread.join();
}
#else
void application::run()
{
	struct sigaction act{};
	act.sa_handler = [](int){
		instance().exit_requested = true;
	};
	sigaction(SIGINT, &act, nullptr);

	while (!is_exit_requested())
	{
		loop();
	}
}
#endif

std::shared_ptr<scene> application::current_scene()
{
	std::unique_lock _{instance().scene_stack_lock};
	if (!instance().scene_stack.empty())
		return instance().scene_stack.back();
	else
		return {};
}

void application::pop_scene()
{
	std::unique_lock _{instance().scene_stack_lock};
	if (!instance().scene_stack.empty())
		instance().scene_stack.pop_back();
}

void application::push_scene(std::shared_ptr<scene> s)
{
	std::unique_lock _{instance().scene_stack_lock};
	instance().scene_stack.push_back(std::move(s));
}

void application::poll_actions()
{
	std::array<XrActionSet, 2> action_sets{
		instance().xr_actionset,
		instance().current_scene()->current_meta.actionset
	};

	instance().xr_session.sync_actions(action_sets);
}

std::optional<std::pair<XrTime, bool>> application::read_action_bool(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	CHECK_XR(xrGetActionStateBoolean(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

std::optional<std::pair<XrTime, float>> application::read_action_float(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	CHECK_XR(xrGetActionStateFloat(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

std::optional<std::pair<XrTime, XrVector2f>> application::read_action_vec2(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	CHECK_XR(xrGetActionStateVector2f(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

void application::haptic_start(XrAction action, XrPath subpath, int64_t duration, float frequency, float amplitude)
{
	if (!is_focused())
		return;

	XrHapticActionInfo action_info{
	        .type = XR_TYPE_HAPTIC_ACTION_INFO,
	        .action = action,
	        .subactionPath = subpath,
	};

	XrHapticVibration vibration{
	        .type = XR_TYPE_HAPTIC_VIBRATION,
	        .duration = duration,
	        .frequency = frequency,
	        .amplitude = amplitude};

	xrApplyHapticFeedback(application::instance().xr_session, &action_info, (XrHapticBaseHeader *)&vibration);
}

void application::haptic_stop(XrAction action, XrPath subpath)
{
	if (!is_focused())
		return;

	XrHapticActionInfo action_info{
	        .type = XR_TYPE_HAPTIC_ACTION_INFO,
	        .action = action,
	        .subactionPath = subpath,
	};

	xrStopHapticFeedback(application::instance().xr_session, &action_info);
}

void application::session_state_changed(XrSessionState new_state, XrTime timestamp)
{
	// See HandleSessionStateChangedEvent
	spdlog::info("Session state changed at timestamp {}: {} => {}", timestamp, xr::to_string(session_state), xr::to_string(new_state));
	session_state = new_state;

	switch (new_state)
	{
		case XR_SESSION_STATE_READY:
			xr_session.begin_session(app_info.viewconfig);
			session_running = true;
			break;

		case XR_SESSION_STATE_SYNCHRONIZED:
			session_visible = false;
			session_focused = false;
			break;

		case XR_SESSION_STATE_VISIBLE:
			session_visible = true;
			session_focused = false;
			break;

		case XR_SESSION_STATE_FOCUSED:
			session_visible = true;
			session_focused = true;
#ifdef __ANDROID__
			set_wifi_locks(true);
#endif
			break;

		case XR_SESSION_STATE_STOPPING:
			session_visible = false;
			session_focused = false;
			xr_session.end_session();
			session_running = false;
#ifdef __ANDROID__
			set_wifi_locks(false);
#endif
			break;

		case XR_SESSION_STATE_EXITING:
			exit_requested = true;
			break;

		case XR_SESSION_STATE_LOSS_PENDING:
			exit_requested = true;
			break;
		default:
			break;
	}
}

void application::interaction_profile_changed()
{
	spdlog::info("Interaction profile changed");

	std::unordered_map<std::string, std::string> current_interaction_profile;

	for (auto device: {"/user/hand/left", "/user/hand/right", "/user/head", "/user/gamepad"})
	{
		try
		{
			std::string current_profile = xr_session.get_current_interaction_profile(device);

			spdlog::info("Current interaction profile for {}: {}", device, current_profile);
		}
		catch (std::exception & e)
		{
			spdlog::warn("Cannot get current interaction profile for {}: {}", device, e.what());
			continue;
		}
	}

	spdlog::info("Global actions:");
	for(auto& [action, type, name]: actions)
	{
		try
		{
			auto sources = xr_session.localized_sources_for_action(action);
			if (!sources.empty())
			{
				spdlog::info("    Sources for {}", name);
				for (auto & k: sources)
					spdlog::info("        {}", k);
			}
			else
				spdlog::info("    No source for {}", name);
		}
		catch(std::exception& e)
		{
			spdlog::warn("Error enumerating sources for {}: {}", name, e.what());
			continue;
		}
	}

	if (current_scene())
	{
		spdlog::info("Actions for current scene ({}):", current_scene()->current_meta.name);

		for(auto& [name, action_and_type]: current_scene()->current_meta.actions_by_name)
		{
			auto [action, type] = action_and_type;

			try
			{
				auto sources = xr_session.localized_sources_for_action(action);
				if (!sources.empty())
				{
					spdlog::info("    Sources for {}", name);
					for (auto & k: sources)
						spdlog::info("        {}", k);
				}
				else
					spdlog::info("    No source for {}", name);
			}
			catch(std::exception& e)
			{
				spdlog::warn("Error enumerating sources for {}: {}", name, e.what());
				continue;
			}
		}
	}
}

void application::poll_events()
{
	xr::event e;
	while (xr_instance.poll_event(e))
	{
		switch (e.header.type)
		{
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				exit_requested = true;
			}
			break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				if (e.interaction_profile_changed.session == xr_session)
					interaction_profile_changed();
				else
					spdlog::error("Received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED for "
					              "unknown session");
			}
			break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				spdlog::warn("XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING in space {} in {:.6}s", magic_enum::enum_name(e.space_changed_pending.referenceSpaceType), (e.space_changed_pending.changeTime - now()) / 1.e9);
			}
			break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				if (e.state_changed.session == xr_session)
					session_state_changed(e.state_changed.state, e.state_changed.time);
				else
					spdlog::error("Received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED for unknown "
					              "session");
			}
			break;
			case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
				spdlog::info("Refresh rate changed from {} to {}",
				             e.refresh_rate_changed.fromDisplayRefreshRate,
				             e.refresh_rate_changed.toDisplayRefreshRate);
			}
			break;
			default:
				spdlog::info("Received event type {}", xr::to_string(e.header.type));
				break;
		}
	}
}
