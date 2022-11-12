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

#include "utils/handle.h"
#include <vulkan/vulkan.h>

namespace vk
{
class device_memory : public utils::handle<VkDeviceMemory>
{
	VkDevice device{};
	void * map = nullptr;

public:
	device_memory() = default;
	device_memory(VkDevice device, const VkMemoryAllocateInfo & allocate_info);
	device_memory(VkDevice device, VkPhysicalDevice physical_device, VkImage target_image, VkMemoryPropertyFlags properties);

	device_memory(VkDevice device, VkPhysicalDevice physical_device, VkBuffer target_buffer, VkMemoryPropertyFlags properties);

	device_memory(const device_memory &) = delete;
	device_memory(device_memory &&) = default;
	device_memory & operator=(const device_memory &) = delete;
	device_memory & operator=(device_memory &&) = default;
	~device_memory();

	void * data()
	{
		return map;
	}

	void map_memory();

	static uint32_t get_memory_type(VkPhysicalDevice physical_device, VkMemoryRequirements requirements, VkMemoryPropertyFlags property_flags);
};
} // namespace vk
