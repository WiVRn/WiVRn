/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "wivrn_vk_bundle.h"

#include "util/u_debug.h"
#include "util/u_logging.h"
#include "wivrn-server_shaders.h"
#include "wivrn_config.h"

#include <format>
#include <ranges>
#include <set>

DEBUG_GET_ONCE_NUM_OPTION(force_gpu_index, "XRT_COMPOSITOR_FORCE_GPU_INDEX", -1)

namespace
{

VkBool32 message_callback(
#if VK_HEADER_VERSION >= 304
        vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
        const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
#else
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
#endif
        void * pUserData)
{
	u_logging_level level = U_LOGGING_ERROR;
	switch (vk::DebugUtilsMessageSeverityFlagBitsEXT(messageSeverity))
	{
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
			level = U_LOGGING_DEBUG;
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
			level = U_LOGGING_INFO;
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
			level = U_LOGGING_WARN;
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
			level = U_LOGGING_ERROR;
			break;
	}
	U_LOG(level, "%s", pCallbackData->pMessage);
	return false;
}
struct strless
{
	bool operator()(const char * a, const char * b) const
	{
		return strcmp(a, b) < 0;
	}
};

int device_type_priority(vk::PhysicalDeviceType device_type)
{
	switch (device_type)
	{
		case vk::PhysicalDeviceType::eDiscreteGpu:
			return 4;
		case vk::PhysicalDeviceType::eIntegratedGpu:
			return 3;
		case vk::PhysicalDeviceType::eVirtualGpu:
			return 2;
		case vk::PhysicalDeviceType::eCpu:
			return 1;
		case vk::PhysicalDeviceType::eOther:
			return 0;
	}
	assert(false);
	return 0;
}

uint32_t select_queue(const std::vector<vk::QueueFamilyProperties> & queues, vk::QueueFlags flags)
{
	uint32_t res = vk::QueueFamilyIgnored;
	auto queue_flag_cost = [](vk::QueueFlags flags) {
		// number of bits set
		return std::popcount(VkQueueFlags(flags));
	};
	for (auto [i, prop]: std::ranges::enumerate_view(queues))
	{
		if ((prop.queueFlags & flags) == flags)
		{
			if (res == vk::QueueFamilyIgnored or
			    queue_flag_cost(prop.queueFlags) < queue_flag_cost(queues[res].queueFlags))
				res = i;
		}
	}
	return res;
}
} // namespace

wivrn::vk_bundle::vk_bundle() :
        instance(nullptr),
        physical_device(nullptr),
        device(nullptr),
        queue(nullptr),
        queue_family_index(vk::QueueFamilyIgnored),
        encode_queue(nullptr),
        encode_queue_family_index(vk::QueueFamilyIgnored),
        debug(nullptr)
{
	// Create instance
	vk::ApplicationInfo app_info{
	        .pApplicationName = "WiVRn server",
	        .pEngineName = "WiVRn",
	        .apiVersion = VK_API_VERSION_1_3,
	};
	{
		// Required extensions
		instance_extensions = {
		        // promoted to 1.1, but we need the KHR name for Monado
		        VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
		        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		};
		// Optional extensions
		std::set<const char *, strless> opt_instance_extensions = {
		        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		};
		for (auto & ext: vk_ctx.enumerateInstanceExtensionProperties())
		{
			if (auto it = opt_instance_extensions.find(ext.extensionName); it != opt_instance_extensions.end())
				instance_extensions.push_back(*it);
		}

		instance = vk::raii::Instance(
		        vk_ctx,
		        vk::InstanceCreateInfo{
		                .pApplicationInfo = &app_info,
		                .enabledExtensionCount = uint32_t(instance_extensions.size()),
		                .ppEnabledExtensionNames = instance_extensions.data(),
		        });
	}

	if (has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		debug = vk::raii::DebugUtilsMessengerEXT(
		        instance,
		        vk::DebugUtilsMessengerCreateInfoEXT{
		                .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
		                .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
		                .pfnUserCallback = message_callback,
		        });

	// Select physical device

	{
		auto phys_devices = instance.enumeratePhysicalDevices();
		if (phys_devices.empty())
			throw std::runtime_error("No Vulkan device");
		auto index = debug_get_num_option_force_gpu_index();
		if (index < 0)
		{
			std::vector<vk::PhysicalDeviceType> types;
			for (const auto & dev: phys_devices)
				types.push_back(dev.getProperties().deviceType);
			index = 0;
			// Select the first device of the highest priority
			for (auto [i, t]: std::ranges::enumerate_view(types))
			{
				if (device_type_priority(t) > device_type_priority(types[index]))
					index = i;
			}
		}
		else if (index > phys_devices.size())
			throw std::runtime_error(std::format("Invalid GPU index {}, must be in range 0..{}", index, phys_devices.size() - 1));
		physical_device = std::move(phys_devices[index]);
	}

	// Select queue families
	{
		auto queues = physical_device.getQueueFamilyProperties();
		queue_family_index = select_queue(queues, vk::QueueFlagBits::eCompute);
#if WIVRN_USE_VULKAN_ENCODE
		encode_queue_family_index = select_queue(queues, vk::QueueFlagBits::eVideoEncodeKHR);
#endif
		// Technically allowed to have a device with only encode or decode capabilities
		if (queue_family_index == vk::QueueFamilyIgnored)
			throw std::runtime_error("GPU does not support vulkan compute");
	}

	// Create logical device
	{
		// Required extensions
		device_extensions = {
		        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
		        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		};
		// Optional extensions
		std::set<const char *, strless> opt_device_extensions = {
		        // For Monado
		        VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
		        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
		        VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
		        VK_KHR_MAINTENANCE_2_EXTENSION_NAME,
		        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
		        VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
		        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
// For FFMPEG
#ifdef VK_EXT_external_memory_dma_buf
		        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
#endif
#ifdef VK_EXT_image_drm_format_modifier
		        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
#endif

// For vulkan video encode
#ifdef VK_KHR_video_queue
		        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_queue
		        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_maintenance1
		        VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_h264
		        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_h265
		        VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_intra_refresh
		        VK_KHR_VIDEO_ENCODE_INTRA_REFRESH_EXTENSION_NAME,
#endif
#ifdef VK_KHR_maintenance9
		        VK_KHR_MAINTENANCE_9_EXTENSION_NAME,
#endif
		};
		for (auto & ext: physical_device.enumerateDeviceExtensionProperties())
		{
			if (auto it = opt_device_extensions.find(ext.extensionName); it != opt_device_extensions.end())
				device_extensions.push_back(*it);
		}

		float prio = 1.0;

		std::vector queues_info = {
		        vk::DeviceQueueCreateInfo{
		                .queueFamilyIndex = queue_family_index,
		                .queueCount = 1,
		                .pQueuePriorities = &prio,
		        },
		};
#if WIVRN_USE_VULKAN_ENCODE
		if (encode_queue_family_index != vk::QueueFamilyIgnored)
		{
			queues_info.push_back(
			        vk::DeviceQueueCreateInfo{
			                .queueFamilyIndex = encode_queue_family_index,
			                .queueCount = 1,
			                .pQueuePriorities = &prio,
			        });

#ifdef VK_KHR_video_maintenance1
			if (has_device_ext(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME))
				std::get<vk::PhysicalDeviceVideoMaintenance1FeaturesKHR>(feat).videoMaintenance1 =
				        std::get<vk::PhysicalDeviceVideoMaintenance1FeaturesKHR>(physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVideoMaintenance1FeaturesKHR>()).videoMaintenance1;
#endif
		}
#endif
#ifdef VK_KHR_maintenance9
		if (has_device_ext(VK_KHR_MAINTENANCE_9_EXTENSION_NAME))
		{
			std::get<vk::PhysicalDeviceMaintenance9FeaturesKHR>(feat).maintenance9 =
			        std::get<vk::PhysicalDeviceMaintenance9FeaturesKHR>(physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceMaintenance9FeaturesKHR>()).maintenance9;
		}
#endif

		// Enable features
		auto [phys_feat, phys_feat12, phys_feat13] = physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();

		std::get<vk::PhysicalDeviceVulkan12Features>(feat).descriptorBindingPartiallyBound = phys_feat12.descriptorBindingPartiallyBound;
		std::get<vk::PhysicalDeviceVulkan12Features>(feat).timelineSemaphore = phys_feat12.timelineSemaphore;
		std::get<vk::PhysicalDeviceVulkan13Features>(feat).synchronization2 = phys_feat13.synchronization2;

#ifdef VK_KHR_video_encode_intra_refresh
		if (has_device_ext(VK_KHR_VIDEO_ENCODE_INTRA_REFRESH_EXTENSION_NAME))
			std::get<vk::PhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR>(feat).videoEncodeIntraRefresh = std::get<vk::PhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR>(physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR>()).videoEncodeIntraRefresh;
#endif

		device = vk::raii::Device(
		        physical_device,
		        vk::DeviceCreateInfo{
		                .pNext = &feat.get(),
		                .queueCreateInfoCount = uint32_t(queues_info.size()),
		                .pQueueCreateInfos = queues_info.data(),
		                .enabledExtensionCount = uint32_t(device_extensions.size()),
		                .ppEnabledExtensionNames = device_extensions.data(),
		        });

		queue = device.getQueue(queue_family_index, 0);
#if WIVRN_USE_VULKAN_ENCODE
		if (encode_queue_family_index != vk::QueueFamilyIgnored)
			encode_queue = device.getQueue(encode_queue_family_index, 0);
#endif
	}

	allocator.emplace(VmaAllocatorCreateInfo{
	                          .physicalDevice = *physical_device,
	                          .device = *device,
	                          .instance = *instance,
	                          .vulkanApiVersion = app_info.apiVersion,
	                  },
	                  *debug != VK_NULL_HANDLE);
}

uint32_t wivrn::vk_bundle::get_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags memory_props)
{
	auto mem_prop = physical_device.getMemoryProperties();

	for (uint32_t i = 0; i < mem_prop.memoryTypeCount; ++i)
	{
		if ((type_bits >> i) & 1)
		{
			if ((mem_prop.memoryTypes[i].propertyFlags & memory_props) ==
			    memory_props)
				return i;
		}
	}
	throw std::runtime_error("Failed to get memory type");
}

bool wivrn::vk_bundle::has_instance_ext(const char * ext) const
{
	for (auto e: instance_extensions)
	{
		if (strcmp(e, ext) == 0)
			return true;
	}
	return false;
}

bool wivrn::vk_bundle::has_device_ext(const char * ext) const
{
	for (auto e: device_extensions)
	{
		if (strcmp(e, ext) == 0)
			return true;
	}
	return false;
}

vk::raii::ShaderModule wivrn::vk_bundle::load_shader(const char * name)
{
	std::span spirv{::shaders.at(name)};

	return vk::raii::ShaderModule{
	        device,
	        vk::ShaderModuleCreateInfo{
	                .codeSize = spirv.size_bytes(),
	                .pCode = spirv.data(),
	        },
	};
}

void wivrn::vk_bundle::name(vk::ObjectType type, uint64_t handle, const char * value)
{
	if (not has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		return;
	device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
	        .objectType = type,
	        .objectHandle = handle,
	        .pObjectName = value,
	});
}
