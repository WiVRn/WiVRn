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

#include "utils/check.h"

#include <utility>
#include <vk_mem_alloc.h>

buffer_allocation::buffer_allocation(const vk::BufferCreateInfo & buffer_info, const VmaAllocationCreateInfo & alloc_info)
{
	VmaAllocator allocator = application::get_allocator();
	VkBuffer tmp;
	CHECK_VK(vmaCreateBuffer(allocator, &static_cast<const VkBufferCreateInfo&>(buffer_info), &alloc_info, &tmp, &allocation, nullptr));
	buffer = vk::raii::Buffer(application::get_device(), tmp);
}

buffer_allocation::~buffer_allocation()
{
	VmaAllocator allocator = application::get_allocator();

	if (data)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyBuffer(allocator, buffer.release(), allocation);
}

buffer_allocation::buffer_allocation(buffer_allocation && other) :
        allocation(other.allocation),
        buffer(std::move(other.buffer)),
        data(other.data)
{
	other.allocation = nullptr;
	other.data = nullptr;
}

void * buffer_allocation::map()
{
	if (data)
		return data;

	VmaAllocator allocator = application::get_allocator();
	CHECK_VK(vmaMapMemory(allocator, allocation, &data));
	return data;
}

const buffer_allocation & buffer_allocation::operator=(buffer_allocation && other)
{
	std::swap(allocation, other.allocation);
	std::swap(buffer, other.buffer);

	return *this;
}

image_allocation::image_allocation(const vk::ImageCreateInfo & image_info, const VmaAllocationCreateInfo & alloc_info)
{
	VmaAllocator allocator = application::get_allocator();
	VkImage tmp;
	CHECK_VK(vmaCreateImage(allocator, &static_cast<const VkImageCreateInfo&>(image_info), &alloc_info, &tmp, &allocation, nullptr));
	image = vk::raii::Image(application::get_device(), tmp);
}

image_allocation::~image_allocation()
{
	VmaAllocator allocator = application::get_allocator();

	if (data)
		vmaUnmapMemory(allocator, allocation);

	vmaDestroyImage(allocator, image.release(), allocation);
}

image_allocation::image_allocation(image_allocation && other) :
        allocation(other.allocation),
        image(std::move(other.image)),
        data(other.data)
{
	other.allocation = nullptr;
	other.data = nullptr;
}

void * image_allocation::map()
{
	if (data)
		return data;

	VmaAllocator allocator = application::get_allocator();
	CHECK_VK(vmaMapMemory(allocator, allocation, &data));
	return data;
}

const image_allocation & image_allocation::operator=(image_allocation && other)
{
	std::swap(allocation, other.allocation);
	std::swap(image, other.image);

	return *this;
}
