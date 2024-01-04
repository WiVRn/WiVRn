/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "allocation.h"
#include "application.h"

#include "spdlog/spdlog.h"
#include "utils/check.h"

#include "vulkan/vulkan_raii.hpp"
#include <utility>
#include <vk_mem_alloc.h>

std::pair<vk::raii::Buffer, VmaAllocation> basic_allocation_traits<VkBuffer>::create(
		const CreateInfo& buffer_info,
		const VmaAllocationCreateInfo* alloc_info)
{
	VmaAllocator allocator = application::get_allocator();
	vk::raii::Device& device = application::get_device();

	VmaAllocation allocation;
	VkBuffer tmp;
	CHECK_VK(vmaCreateBuffer(allocator, &(NativeCreateInfo&)buffer_info, alloc_info, &tmp, &allocation, nullptr));

	return std::pair<vk::raii::Buffer, VmaAllocation>{vk::raii::Buffer{device, tmp}, allocation};

}

void basic_allocation_traits<VkBuffer>::destroy(
	vk::raii::Buffer& buffer,
	VmaAllocation allocation,
	void * mapped)
{
	VmaAllocator allocator = application::get_allocator();

	if (mapped)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyBuffer(allocator, buffer.release(), allocation);
}

void * basic_allocation_traits_base::map(VmaAllocation allocation)
{
	VmaAllocator allocator = application::get_allocator();

	void * mapped;
	CHECK_VK(vmaMapMemory(allocator, allocation, &mapped));
	return mapped;
}

void basic_allocation_traits_base::unmap(VmaAllocation allocation)
{
	VmaAllocator allocator = application::get_allocator();

	vmaUnmapMemory(allocator, allocation);
}

std::pair<vk::raii::Image, VmaAllocation> basic_allocation_traits<VkImage>::create(
		const CreateInfo& image_info,
		const VmaAllocationCreateInfo* alloc_info)
{
	VmaAllocator allocator = application::get_allocator();
	vk::raii::Device& device = application::get_device();

	VmaAllocation allocation;
	VkImage tmp;
	CHECK_VK(vmaCreateImage(allocator, &(NativeCreateInfo&)image_info, alloc_info, &tmp, &allocation, nullptr));

	return std::pair<vk::raii::Image, VmaAllocation>{vk::raii::Image{device, tmp}, allocation};
}

void basic_allocation_traits<VkImage>::destroy(
	vk::raii::Image& image,
	VmaAllocation allocation,
	void * mapped)
{
	VmaAllocator allocator = application::get_allocator();

	if (mapped)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyImage(allocator, image.release(), allocation);
}
