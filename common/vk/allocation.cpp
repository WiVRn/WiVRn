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

#include "vk/check.h"

#include "vulkan/vulkan_raii.hpp"
#include <utility>
#include <vk_mem_alloc.h>

std::pair<vk::raii::Buffer, VmaAllocation> basic_allocation_traits<VkBuffer>::create(
        vk::raii::Device & device,
        const CreateInfo & buffer_info,
        const VmaAllocationCreateInfo & alloc_info)
{
	VmaAllocator allocator = vk_allocator::instance();

	VmaAllocation allocation;
	VkBuffer tmp;

	CHECK_VK(vmaCreateBuffer(allocator, &(NativeCreateInfo &)buffer_info, &alloc_info, &tmp, &allocation, nullptr));

	return std::pair<vk::raii::Buffer, VmaAllocation>{vk::raii::Buffer{device, tmp}, allocation};
}

void basic_allocation_traits<VkBuffer>::destroy(
        vk::raii::Buffer & buffer,
        VmaAllocation allocation,
        void * mapped)
{
	VmaAllocator allocator = vk_allocator::instance();

	if (mapped)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyBuffer(allocator, buffer.release(), allocation);
}

void * basic_allocation_traits_base::map(VmaAllocation allocation)
{
	VmaAllocator allocator = vk_allocator::instance();

	void * mapped;
	CHECK_VK(vmaMapMemory(allocator, allocation, &mapped));
	return mapped;
}

void basic_allocation_traits_base::unmap(VmaAllocation allocation)
{
	VmaAllocator allocator = vk_allocator::instance();

	vmaUnmapMemory(allocator, allocation);
}

std::pair<vk::raii::Image, VmaAllocation> basic_allocation_traits<VkImage>::create(
        vk::raii::Device & device,
        const CreateInfo & image_info,
        const VmaAllocationCreateInfo & alloc_info)
{
	VmaAllocator allocator = vk_allocator::instance();

	VmaAllocation allocation;
	VkImage tmp;

	CHECK_VK(vmaCreateImage(allocator, &(NativeCreateInfo &)image_info, &alloc_info, &tmp, &allocation, nullptr));

	return std::pair<vk::raii::Image, VmaAllocation>{vk::raii::Image{device, tmp}, allocation};
}

void basic_allocation_traits<VkImage>::destroy(
        vk::raii::Image & image,
        VmaAllocation allocation,
        void * mapped)
{
	VmaAllocator allocator = vk_allocator::instance();

	if (mapped)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyImage(allocator, image.release(), allocation);
}
