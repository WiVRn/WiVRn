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
#include "external/magic_enum.hpp"
#include "scene.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "utils/ranges.h"
#include "utils/strings.h"
#include "vk/details/enumerate.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include "vk/vk.h"
#include "xr/xr.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <openxr/openxr_platform.h>

#ifndef NDEBUG
#include "utils/backtrace.h"
#endif

#ifdef XR_USE_PLATFORM_ANDROID
#include <android/native_activity.h>
#endif

using namespace std::chrono_literals;

application * application::instance_ = nullptr;

#ifdef XR_USE_PLATFORM_ANDROID
jni_thread::jni_thread(application & app) :
        vm(app.app_info.native_app->activity->vm)
{
	vm->AttachCurrentThread(&env, nullptr);
}

jni_thread::jni_thread() :
        jni_thread(application::instance())
{
}

jni_thread::~jni_thread()
{
	vm->DetachCurrentThread();
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
	if (instance_->debug_report_ignored_objects.contains(object))
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

	auto it = instance_->debug_report_object_name.find(object);
	if (it != instance_->debug_report_object_name.end())
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
	bool validation_layer_found = false;
	for (VkLayerProperties & i: vk::details::enumerate<VkLayerProperties>(vkEnumerateInstanceLayerProperties))
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
	for (VkExtensionProperties & i: vk::details::enumerate<VkExtensionProperties>(vkEnumerateInstanceExtensionProperties, nullptr))
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

	VkApplicationInfo applicationInfo{};
	applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	applicationInfo.apiVersion = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(vulkan_version), XR_VERSION_MINOR(vulkan_version), 0);
	applicationInfo.pApplicationName = app_info.name.c_str();
	applicationInfo.applicationVersion = app_info.version;
	applicationInfo.pEngineName = engine_name;
	applicationInfo.engineVersion = engine_version;

	VkInstanceCreateInfo instanceCreateInfo{};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	instanceCreateInfo.enabledLayerCount = layers.size();
	instanceCreateInfo.ppEnabledLayerNames = layers.data();
	instanceCreateInfo.enabledExtensionCount = instance_extensions.size();
	instanceCreateInfo.ppEnabledExtensionNames = instance_extensions.data();

	XrVulkanInstanceCreateInfoKHR create_info{};
	create_info.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
	create_info.systemId = xr_system_id;
	create_info.createFlags = 0;
	create_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
	create_info.vulkanCreateInfo = &instanceCreateInfo;
	create_info.vulkanAllocator = nullptr;

	auto xrCreateVulkanInstanceKHR =
	        xr_instance.get_proc<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");

	VkResult vresult;
	XrResult xresult = xrCreateVulkanInstanceKHR(xr_instance, &create_info, &vk_instance, &vresult);
	CHECK_VK(vresult, "xrCreateVulkanInstanceKHR");
	CHECK_XR(xresult, "xrCreateVulkanInstanceKHR");

#ifndef NDEBUG
	if (debug_report_found)
	{
		VkDebugReportCallbackCreateInfoEXT debug_report_info{
		        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
		        .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
		                 VK_DEBUG_REPORT_WARNING_BIT_EXT |
		                 VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
		                 VK_DEBUG_REPORT_ERROR_BIT_EXT |
		                 VK_DEBUG_REPORT_DEBUG_BIT_EXT,
		        .pfnCallback = vulkan_debug_report_callback,
		};
		VkDebugReportCallbackEXT debug_report_callback;
		auto vkCreateDebugReportCallbackEXT = get_vulkan_proc<PFN_vkCreateDebugReportCallbackEXT>("vkCreateDebugReportCallbackEXT");
		CHECK_VK(vkCreateDebugReportCallbackEXT(vk_instance, &debug_report_info, nullptr, &debug_report_callback));
	}
#endif

	vk_physical_device = xr_system_id.physical_device(vk_instance);

	spdlog::info("Available Vulkan device extensions:");
	for (VkExtensionProperties & i: vk::details::enumerate<VkExtensionProperties>(vkEnumerateDeviceExtensionProperties, vk_physical_device, nullptr))
	{
		spdlog::info("    {}", i.extensionName);
	}

	VkPhysicalDeviceProperties prop;
	vkGetPhysicalDeviceProperties(vk_physical_device, &prop);
	spdlog::info("Initializing Vulkan with device {}", prop.deviceName);

	uint32_t count;
	vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_properties(count);
	vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, queue_properties.data());

	vk_queue_family_index = -1;
	[[maybe_unused]] bool vk_queue_found = false;
	for (size_t i = 0; i < queue_properties.size(); i++)
	{
		if (queue_properties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT)
		{
			vk_queue_found = true;
			vk_queue_family_index = i;
			break;
		}
	}
	assert(vk_queue_found);
	spdlog::info("Using queue family {}", vk_queue_family_index);

	float queuePriority = 0.0f;

	VkDeviceQueueCreateInfo queueCreateInfo{};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = vk_queue_family_index;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR ycbcr_feature{
	        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR,
	        .samplerYcbcrConversion = VK_TRUE};

	VkPhysicalDeviceFeatures deviceFeatures{};

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &ycbcr_feature;
	createInfo.pQueueCreateInfos = &queueCreateInfo;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = device_extensions.size();
	createInfo.ppEnabledExtensionNames = device_extensions.data();

	vk_device = xr_system_id.create_device(vk_physical_device, createInfo);

	vkGetDeviceQueue(vk_device, vk_queue_family_index, 0, &vk_queue);
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

	swapchain_format = VK_FORMAT_UNDEFINED;
	for (auto i: xr_session.get_swapchain_formats())
	{
		if (std::find(supported_formats.begin(), supported_formats.end(), i) != supported_formats.end())
		{
			swapchain_format = i;
			break;
		}
	}

	if (swapchain_format == VK_FORMAT_UNDEFINED)
		throw std::runtime_error("No supported swapchain format");

	spdlog::info("Using format {}", string_VkFormat(swapchain_format));

	// XrViewConfigurationProperties view_props = xr_system_id.view_configuration_properties(viewconfig);

	auto views = xr_system_id.view_configuration_views(app_info.viewconfig);

	xr_swapchains.reserve(views.size());
	for (auto & view: views)
	{
		xr_swapchains.emplace_back(xr_session, vk_device, swapchain_format, view.recommendedImageRectWidth, view.recommendedImageRectHeight);

		spdlog::info("Created swapchain {}: {}x{}", xr_swapchains.size(), xr_swapchains.back().width(), xr_swapchains.back().height());
	}

	vk_cmdpool = vk::command_pool{vk_device, vk_queue_family_index};

	// TODO get it from application info
	xr_actionset = xr::actionset(xr_instance, "actions", "Actions");
	std::vector<XrActionSuggestedBinding> bindings;

	for (const auto & [name, type]: oculus_touch)
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
			action_spaces.push_back(xr_session.create_action_space(a));
		}

		bindings.push_back({a, xr_instance.string_to_path(name)});
	}

	xr_instance.suggest_bindings("/interaction_profiles/oculus/touch_controller", bindings);

	xr_session.attach_actionsets({xr_actionset});

	interaction_profile_changed();
}

std::pair<XrAction, XrActionType> application::get_action(const std::string & requested_name)
{
	for (const auto & [action, type, name]: instance_->actions)
	{
		if (name == requested_name)
			return {action, type};
	}

	return {};
}

application::application(application_info info) :
        app_info(std::move(info)), jni(*this)
{
#ifdef XR_USE_PLATFORM_ANDROID
	// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/types.html

	JNIEnv * Env = jni.get_JNIEnv();
	ANativeActivity * activity = app_info.native_app->activity;

	jmethodID jtmID;

	jclass jNativeClass = Env->GetObjectClass(activity->clazz);
	jtmID = Env->GetMethodID(jNativeClass, "getApplication", "()Landroid/app/Application;");

	jobject jNativeApplication = (jobject)Env->CallObjectMethod(activity->clazz, jtmID);
	jtmID = Env->GetMethodID(Env->GetObjectClass(jNativeApplication), "getApplicationContext", "()Landroid/content/Context;");

	jobject jNativeContext = (jobject)Env->CallObjectMethod(jNativeApplication, jtmID);
	jfieldID jNativeWIFI_SERVICE_fid = Env->GetStaticFieldID(Env->GetObjectClass(jNativeContext), "WIFI_SERVICE", "Ljava/lang/String;");
	jstring jNativeSFID_jstr = (jstring)Env->GetStaticObjectField(Env->FindClass("android/content/Context"), jNativeWIFI_SERVICE_fid);
	jstring wifiLockjStr = Env->NewStringUTF("WIVRn");

	jtmID = Env->GetMethodID(Env->FindClass("android/content/Context"), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
	jobject jSystemService = Env->CallObjectMethod(jNativeContext, jtmID, jNativeSFID_jstr);

	jclass jWMClass = Env->FindClass("android/net/wifi/WifiManager");
	jclass jWMMLClass = Env->FindClass("android/net/wifi/WifiManager$MulticastLock");
	jtmID = Env->GetMethodID(jWMClass, "createMulticastLock", "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;");
	jobject jMCObj = Env->CallObjectMethod(jSystemService, jtmID, wifiLockjStr);

	jtmID = Env->GetMethodID(jWMMLClass, "setReferenceCounted", "(Z)V");
	Env->CallVoidMethod(jMCObj, jtmID, 0);

	jtmID = Env->GetMethodID(jWMMLClass, "acquire", "()V");
	Env->CallVoidMethod(jMCObj, jtmID);
	jtmID = Env->GetMethodID(jWMMLClass, "isHeld", "()Z");
	jboolean isheld = Env->CallBooleanMethod(jMCObj, jtmID);
	if (isheld)
	{
		spdlog::info("MulticastLock acquired");
	}
	else
	{
		spdlog::info("MulticastLock is not acquired");
	}

	jclass jWMWLClass = Env->FindClass("android/net/wifi/WifiManager$WifiLock");
	jtmID = Env->GetMethodID(jWMClass, "createWifiLock", "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;");
	// jobject jMCObj2 = Env->CallObjectMethod(jSystemService, jtmID, 4 /* WIFI_MODE_FULL_LOW_LATENCY */, wifiLockjStr);
	jobject jMCObj2 = Env->CallObjectMethod(jSystemService, jtmID, 3 /* WIFI_MODE_FULL_HIGH_PERF */, wifiLockjStr);

	jtmID = Env->GetMethodID(jWMWLClass, "setReferenceCounted", "(Z)V");
	Env->CallVoidMethod(jMCObj2, jtmID, 0);

	jtmID = Env->GetMethodID(jWMWLClass, "acquire", "()V");
	Env->CallVoidMethod(jMCObj2, jtmID);
	jtmID = Env->GetMethodID(jWMWLClass, "isHeld", "()Z");
	isheld = Env->CallBooleanMethod(jMCObj2, jtmID);
	if (isheld)
	{
		spdlog::info("WifiLock low latency acquired");
	}
	else
	{
		spdlog::info("WifiLock low latency is not acquired");
	}

	Env->DeleteLocalRef(wifiLockjStr);

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
		XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid{};
		loaderInitInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
		loaderInitInfoAndroid.next = nullptr;
		loaderInitInfoAndroid.applicationVM = app_info.native_app->activity->vm;
		loaderInitInfoAndroid.applicationContext = app_info.native_app->activity->clazz;
		initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInitInfoAndroid);
	}
#endif

	assert(instance_ == nullptr);
	instance_ = this;

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

void application::cleanup()
{
	// The Vulkan device and instance are destroyed by the OpenXR runtime

#ifdef XR_USE_PLATFORM_ANDROID
	app_info.native_app->activity->vm->DetachCurrentThread();
#endif
	assert(instance_ == this);
	instance_ = nullptr;
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
		jni_thread jni;

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
	std::unique_lock _{instance_->scene_stack_lock};
	if (!instance_->scene_stack.empty())
		return instance_->scene_stack.back();
	else
		return {};
}

void application::pop_scene()
{
	std::unique_lock _{instance_->scene_stack_lock};
	if (!instance_->scene_stack.empty())
		instance_->scene_stack.pop_back();
}

void application::push_scene(std::shared_ptr<scene> s)
{
	std::unique_lock _{instance_->scene_stack_lock};
	instance_->scene_stack.push_back(std::move(s));
}

void application::poll_actions()
{
	instance_->xr_session.sync_actions(instance_->xr_actionset);
}

bool application::read_action(XrAction action, bool & value)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	CHECK_XR(xrGetActionStateBoolean(instance_->xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
	return true;
}

bool application::read_action(XrAction action, float & value)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	CHECK_XR(xrGetActionStateFloat(instance_->xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
	return true;
}

bool application::read_action(XrAction action, XrVector2f & value)
{
	if (!is_focused())
		return false;

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	CHECK_XR(xrGetActionStateVector2f(instance_->xr_session, &get_info, &state));

	if (!state.isActive)
		return false;

	value = state.currentState;
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

	xrApplyHapticFeedback(application::instance_->xr_session, &action_info, (XrHapticBaseHeader *)&vibration);
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

	xrStopHapticFeedback(application::instance_->xr_session, &action_info);
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
			break;

		case XR_SESSION_STATE_STOPPING:
			session_visible = false;
			session_focused = false;
			xr_session.end_session();
			session_running = false;
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
