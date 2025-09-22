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
#include "vk/vk_helpers.h"

#include "wivrn_vk_bundle.h"

#include "util/u_logging.h"
#include <string>

namespace
{
[[maybe_unused]] vk::raii::Queue make_queue(vk::raii::Device & device, VkQueue queue, uint32_t family_index, uint32_t index)
{
	if (queue == VK_NULL_HANDLE)
		return nullptr;
	return vk::raii::Queue(device, family_index, index);
}

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

vk::raii::DebugUtilsMessengerEXT register_message_callback(vk::raii::Instance & instance, vk_bundle & vk)
{
	if (vk.has_EXT_debug_utils)
		return vk::raii::DebugUtilsMessengerEXT(
		        instance, vk::DebugUtilsMessengerCreateInfoEXT{
		                          .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
		                          .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
		                          .pfnUserCallback = message_callback,
		                  });
	return nullptr;
}
} // namespace

wivrn::wivrn_vk_bundle::wivrn_vk_bundle(vk_bundle & vk, std::span<const char *> requested_instance_extensions, std::span<const char *> requested_device_extensions) :
        vk(vk),
        instance(vk_ctx, vk.instance),
        physical_device(instance, vk.physical_device),
        device(physical_device, vk.device),
        allocator({
                          .physicalDevice = vk.physical_device,
                          .device = vk.device,
                          .instance = vk.instance,
                          .vulkanApiVersion = VK_MAKE_VERSION(1, 3, 0), // FIXME: sync with wivrn_session.cpp
                  },
                  vk.has_EXT_debug_utils),
        queue(device, vk.main_queue.family_index, vk.main_queue.index),
        queue_family_index(vk.main_queue.family_index),
#ifdef VK_KHR_video_encode_queue
        encode_queue(make_queue(device, vk.encode_queue.queue, vk.encode_queue.family_index, vk.encode_queue.index)),
        encode_queue_family_index(vk.encode_queue.family_index),
#else
        encode_queue(nullptr),
        encode_queue_family_index(vk::QueueFamilyIgnored),
#endif
        debug(register_message_callback(instance, vk))
{
	// This is manually synced with monado code

	// Instance extensions
#ifdef VK_EXT_display_surface_counter
	if (vk.has_EXT_display_surface_counter)
		instance_extensions.push_back(VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
#endif
#ifdef VK_EXT_swapchain_colorspace
	if (vk.has_EXT_swapchain_colorspace)
		instance_extensions.push_back(VK_EXT_SWAPCHAIN_COLORSPACE_EXTENSION_NAME);
#endif
#ifdef VK_EXT_debug_utils
	if (vk.has_EXT_debug_utils)
		instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
	for (auto & ext: vk_ctx.enumerateInstanceExtensionProperties())
	{
		for (const char * requested: requested_instance_extensions)
		{
			if (std::string(requested) == ext.extensionName)
			{
				instance_extensions.push_back(requested);
				break;
			}
		}
	}

	// Device extensions
#ifdef VK_KHR_external_fence_fd
	if (vk.has_KHR_external_fence_fd)
		device_extensions.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
#endif
#ifdef VK_KHR_external_semaphore_fd
	if (vk.has_KHR_external_semaphore_fd)
		device_extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
#endif
#ifdef VK_KHR_format_feature_flags2
	if (vk.has_KHR_format_feature_flags2)
		device_extensions.push_back(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
#endif
#ifdef VK_KHR_global_priority
	if (vk.has_KHR_global_priority)
		device_extensions.push_back(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME);
#endif
#ifdef VK_KHR_image_format_list
	if (vk.has_KHR_image_format_list)
		device_extensions.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
#endif
#ifdef VK_KHR_maintenance1
	if (vk.has_KHR_maintenance1)
		device_extensions.push_back(VK_KHR_MAINTENANCE_1_EXTENSION_NAME);
#endif
#ifdef VK_KHR_maintenance2
	if (vk.has_KHR_maintenance2)
		device_extensions.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
#endif
#ifdef VK_KHR_maintenance3
	if (vk.has_KHR_maintenance3)
		device_extensions.push_back(VK_KHR_MAINTENANCE_3_EXTENSION_NAME);
#endif
#ifdef VK_KHR_maintenance4
	if (vk.has_KHR_maintenance4)
		device_extensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
#endif
#ifdef VK_KHR_timeline_semaphore
	if (vk.has_KHR_timeline_semaphore)
		device_extensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
#endif
#ifdef VK_EXT_calibrated_timestamps
	if (vk.has_EXT_calibrated_timestamps)
		device_extensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
#endif
#ifdef VK_EXT_display_control
	if (vk.has_EXT_display_control)
		device_extensions.push_back(VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME);
#endif
#ifdef VK_EXT_external_memory_dma_buf
	if (vk.has_EXT_external_memory_dma_buf)
		device_extensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
#endif
#ifdef VK_EXT_global_priority
	if (vk.has_EXT_global_priority)
		device_extensions.push_back(VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME);
#endif
#ifdef VK_EXT_image_drm_format_modifier
	if (vk.has_EXT_image_drm_format_modifier)
		device_extensions.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
#endif
#ifdef VK_EXT_robustness2
	if (vk.has_EXT_robustness2)
		device_extensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
#endif
#ifdef VK_GOOGLE_display_timing
	if (vk.has_GOOGLE_display_timing)
		device_extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
#endif

	for (auto & ext: physical_device.enumerateDeviceExtensionProperties())
	{
		for (const char * requested: requested_device_extensions)
		{
			if (std::string(requested) == ext.extensionName)
			{
				device_extensions.push_back(requested);
				break;
			}
		}
	}
}

uint32_t wivrn::wivrn_vk_bundle::get_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags memory_props)
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

void wivrn::wivrn_vk_bundle::name(vk::ObjectType type, uint64_t handle, const char * value)
{
	if (not vk.has_EXT_debug_utils)
		return;
	device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
	        .objectType = type,
	        .objectHandle = handle,
	        .pObjectName = value,
	});
}
