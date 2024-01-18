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

#include "application.h"
#include "hardware.h"
#include "magic_enum.hpp"
#include "scene.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "utils/ranges.h"
#include "vk/vk.h"
#include "xr/xr.h"
#include <algorithm>
#include <thread>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <openxr/openxr_platform.h>

#ifndef NDEBUG
#include "utils/backtrace.h"
#endif

#ifdef XR_USE_PLATFORM_ANDROID
#include <android/native_activity.h>
#include <sys/system_properties.h>

#include "jnipp.h"
#endif

using namespace std::chrono_literals;

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

#ifdef XR_USE_PLATFORM_ANDROID
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

	vk::PhysicalDeviceFeatures device_features;

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

	VmaAllocatorCreateInfo info{
		.physicalDevice = *vk_physical_device,
		.device = *vk_device,
		.instance = *vk_instance,
	};
	CHECK_VK(vmaCreateAllocator(&info, &allocator));
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

void application::initialize()
{
	// LogLayersAndExtensions
	assert(!xr_instance);
	std::vector<const char *> extensions{XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME /*, XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME*/};

	for (XrExtensionProperties & i: xr::instance::extensions())
	{
		if (!strcmp(i.extensionName, XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME))
			extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
		if (!strcmp(i.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
			extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
	}

#ifdef XR_USE_PLATFORM_ANDROID
	xr_instance =
	        xr::instance(app_info.name, app_info.native_app->activity->vm, app_info.native_app->activity->clazz, extensions);
#else
	xr_instance = xr::instance(app_info.name, extensions);
#endif

	spdlog::info("Created OpenXR instance, runtime {}, version {}", xr_instance.get_runtime_name(), xr_instance.get_runtime_version());

	xr_system_id = xr::system(xr_instance, app_info.formfactor);
	spdlog::info("Created OpenXR system for form factor {}", xr::to_string(app_info.formfactor));

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

	swapchain_format = vk::Format::eUndefined;
	for (auto i: xr_session.get_swapchain_formats())
	{
		if (std::find(supported_formats.begin(), supported_formats.end(), i) != supported_formats.end())
		{
			swapchain_format = i;
			break;
		}
	}

	if (swapchain_format == vk::Format::eUndefined)
		throw std::runtime_error("No supported swapchain format");

	spdlog::info("Using format {}", string_VkFormat(swapchain_format));

	// XrViewConfigurationProperties view_props = xr_system_id.view_configuration_properties(viewconfig);

	auto views = xr_system_id.view_configuration_views(app_info.viewconfig);

	xr_swapchains.reserve(views.size());
	for (auto view: views)
	{
		view = override_view(view, guess_model());
		xr_swapchains.emplace_back(xr_session, vk_device, swapchain_format, view.recommendedImageRectWidth, view.recommendedImageRectHeight);

		spdlog::info("Created swapchain {}: {}x{}", xr_swapchains.size(), xr_swapchains.back().width(), xr_swapchains.back().height());
	}

	vk::CommandPoolCreateInfo cmdpool_create_info;
	cmdpool_create_info.queueFamilyIndex = vk_queue_family_index;
	cmdpool_create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	vk_cmdpool = vk::raii::CommandPool{vk_device, cmdpool_create_info};

	// TODO get it from application info
	xr_actionset = xr::actionset(xr_instance, "actions", "Actions");

	switch (guess_model())
	{
		case model::oculus_quest:
		case model::oculus_quest_2:
		case model::meta_quest_pro:
		case model::meta_quest_3: {
			spdlog::info("Suggesting oculus/touch_controller bindings");
			std::vector<XrActionSuggestedBinding> touch_controller_bindings;
			for (const auto & [name, type]: oculus_touch)
			{
				process_binding_action(touch_controller_bindings, name, type);
			}
			xr_instance.suggest_bindings("/interaction_profiles/oculus/touch_controller", touch_controller_bindings);
		}
		break;

		case model::pico_neo_3: {
			spdlog::info("Suggesting Pico Neo 3 bindings");
			std::vector<XrActionSuggestedBinding> pico_neo_3_bindings;
			for (const auto & [name, type]: pico_neo_3)
			{
				process_binding_action(pico_neo_3_bindings, name, type);
			}
			xr_instance.suggest_bindings("/interaction_profiles/bytedance/pico_neo3_controller", pico_neo_3_bindings);
		}
		break;

		case model::pico_4: {
			spdlog::info("Suggesting Pico 4 bindings");
			std::vector<XrActionSuggestedBinding> pico_4_bindings;
			for (const auto & [name, type]: pico_4)
			{
				process_binding_action(pico_4_bindings, name, type);
			}
			xr_instance.suggest_bindings("/interaction_profiles/bytedance/pico4_controller", pico_4_bindings);
		}
		break;

		case model::unknown: {
			spdlog::info("Suggesting Khronos simple controller bindings");
			std::vector<XrActionSuggestedBinding> simple_controller_bindings;
			for (const auto & [name, type]: simple_controller)
			{
				process_binding_action(simple_controller_bindings, name, type);
			}
			xr_instance.suggest_bindings("/interaction_profiles/khr/simple_controller", simple_controller_bindings);
		}
		break;
	}

	xr_session.attach_actionsets({xr_actionset});

	interaction_profile_changed();
}

void application::process_binding_action(std::vector<XrActionSuggestedBinding> & bindings,
                                         const char * name,
                                         const XrActionType & type)
{
	std::string name_without_slashes = std::string(name).substr(1);

	for (char & i: name_without_slashes)
	{
		if (i == '/')
			i = '_';
	}

	auto a = xr_actionset.create_action(type, name_without_slashes);
	actions.emplace_back(a, type, name);

	if (type == XR_ACTION_TYPE_POSE_INPUT)
	{
		if (!strcmp(name, "/user/hand/left/input/grip/pose"))
			left_grip_space = xr_session.create_action_space(a);
		else if (!strcmp(name, "/user/hand/left/input/aim/pose"))
			left_aim_space = xr_session.create_action_space(a);
		else if (!strcmp(name, "/user/hand/right/input/grip/pose"))
			right_grip_space = xr_session.create_action_space(a);
		else if (!strcmp(name, "/user/hand/right/input/aim/pose"))
			right_aim_space = xr_session.create_action_space(a);
	}

	bindings.push_back({a, xr_instance.string_to_path(name)});
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
#ifdef XR_USE_PLATFORM_ANDROID
	// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/types.html

	setup_jni();
	jni::object<""> act(app_info.native_app->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");

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
#endif

	try
	{
		initialize();
	}
	catch (...)
	{
		cleanup();
		throw;
	}
}

#ifdef XR_USE_PLATFORM_ANDROID
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
	// The Vulkan device and instance are destroyed by the OpenXR runtime

#ifdef XR_USE_PLATFORM_ANDROID
	jni::jni_thread::detach();
#endif
}

application::~application()
{
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
		poll_actions();

		auto scene = current_scene();
		if (scene)
		{
			if (scene != last_scene)
			{
				if (last_scene)
					last_scene->on_unfocused();

				scene->on_focused();

				last_scene = scene;
			}

			scene->render();
		}
		else
		{
			exit_requested = true;
		}
	}
}

#ifdef XR_USE_PLATFORM_ANDROID
void application::run()
{
	std::thread application_thread{[&]() {
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
	}};
	pthread_setname_np(application_thread.native_handle(), "application_thread");

	// Read all pending events.
	while (!exit_requested)
	{
		int events;
		struct android_poll_source * source;

		while (ALooper_pollAll(-1, nullptr, &events, (void **)&source) >= 0)
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
	instance().xr_session.sync_actions(instance().xr_actionset);
}

bool application::read_action(XrAction action, bool & value, XrTime & last_change_time)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	CHECK_XR(xrGetActionStateBoolean(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
	last_change_time = state.lastChangeTime;
	return true;
}

bool application::read_action(XrAction action, float & value, XrTime & last_change_time)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	CHECK_XR(xrGetActionStateFloat(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
	last_change_time = state.lastChangeTime;
	return true;
}

bool application::read_action(XrAction action, XrVector2f & value, XrTime & last_change_time)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	CHECK_XR(xrGetActionStateVector2f(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
	last_change_time = state.lastChangeTime;
	return true;
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
#ifdef XR_USE_PLATFORM_ANDROID
			set_wifi_locks(true);
#endif
			break;

		case XR_SESSION_STATE_STOPPING:
			session_visible = false;
			session_focused = false;
			xr_session.end_session();
			session_running = false;
#ifdef XR_USE_PLATFORM_ANDROID
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

	for (auto i: {"/user/hand/left", "/user/hand/right", "/user/head", "/user/gamepad"})
	{
		try
		{
			spdlog::info("Current interaction profile for {}: {}", i, xr_session.get_current_interaction_profile(i));
		}
		catch (std::exception & e)
		{
			spdlog::warn("Cannot get current interaction profile for {}: {}", i, e.what());
			continue;
		}
	}

	for (auto [i, j]: utils::zip(actions, oculus_touch))
	{
		auto sources = xr_session.localized_sources_for_action(std::get<XrAction>(i));
		if (!sources.empty())
		{
			spdlog::info("    Sources for {}", j.first);
			for (auto & k: sources)
			{
				spdlog::info("        {}", k);
			}
		}
		else
		{
			spdlog::warn("    No source for {}", j.first);
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
			default:
				spdlog::info("Received event type {}", xr::to_string(e.header.type));
				break;
		}
	}
}
