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

#include "device_memory.h"
#include "utils/check.h"

uint32_t vk::device_memory::get_memory_type(VkPhysicalDevice physical_device, VkMemoryRequirements requirements, VkMemoryPropertyFlags property_flags)
{
	uint32_t types = requirements.memoryTypeBits;

	VkPhysicalDeviceMemoryProperties memory_properties{};
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t type = 0; type < memory_properties.memoryTypeCount; ++type)
	{
		if (not((1 << type) & types))
			continue;

		const auto & properties = memory_properties.memoryTypes[type];
		if ((properties.propertyFlags & property_flags) == property_flags)
			return type;
	}

	throw std::runtime_error{"No Vulkan memory with required flags"};
}

vk::device_memory::device_memory(VkDevice device, const VkMemoryAllocateInfo & allocate_info) :
        device(device)
{
	CHECK_VK(vkAllocateMemory(device, &allocate_info, nullptr, &id));
}

vk::device_memory::device_memory(VkDevice device, VkPhysicalDevice physical_device, VkImage target_image, VkMemoryPropertyFlags properties) :
        device(device)
{
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(device, target_image, &requirements);

	uint32_t memory_type = get_memory_type(physical_device, requirements, properties);

	VkMemoryDedicatedAllocateInfoKHR dedicated_allocate_info{};
	dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicated_allocate_info.image = target_image;

	VkMemoryAllocateInfo memory_allocate_info{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = requirements.size;
	memory_allocate_info.memoryTypeIndex = memory_type;
	memory_allocate_info.pNext = &dedicated_allocate_info;

	CHECK_VK(vkAllocateMemory(device, &memory_allocate_info, nullptr, &id));

	try
	{
		CHECK_VK(vkBindImageMemory(device, target_image, id, 0));
	}
	catch (...)
	{
		vkFreeMemory(device, id, nullptr);
		throw;
	}
}

vk::device_memory::device_memory(VkDevice device, VkPhysicalDevice physical_device, VkBuffer target_buffer, VkMemoryPropertyFlags properties) :
        device(device)
{
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(device, target_buffer, &requirements);

	uint32_t memory_type = get_memory_type(physical_device, requirements, properties);

	VkMemoryDedicatedAllocateInfoKHR dedicated_allocate_info{};
	dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicated_allocate_info.buffer = target_buffer;

	VkMemoryAllocateInfo memory_allocate_info{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = requirements.size;
	memory_allocate_info.memoryTypeIndex = memory_type;
	memory_allocate_info.pNext = &dedicated_allocate_info;

	CHECK_VK(vkAllocateMemory(device, &memory_allocate_info, nullptr, &id));

	try
	{
		CHECK_VK(vkBindBufferMemory(device, target_buffer, id, 0));
	}
	catch (...)
	{
		vkFreeMemory(device, id, nullptr);
		throw;
	}
}

vk::device_memory::~device_memory()
{
	if (device)
	{
		if (id and map)
		{
			vkUnmapMemory(device, id);
		}
		vkFreeMemory(device, id, nullptr);
	}
}

void vk::device_memory::map_memory()
{
	CHECK_VK(vkMapMemory(device, id, 0, VK_WHOLE_SIZE, 0, (void **)&map));
}
