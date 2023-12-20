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

#pragma once

#include "vk_mem_alloc.h"
#include <vulkan/vulkan_raii.hpp>

class buffer_allocation
{
	VmaAllocation allocation = nullptr;
	vk::raii::Buffer buffer = nullptr;
	void * data = nullptr;

public:
	operator VkBuffer() const
	{
		return *buffer;
	}

	operator vk::Buffer() const
	{
		return *buffer;
	}

	operator const vk::raii::Buffer&() const
	{
		return buffer;
	}

	operator vk::raii::Buffer&()
	{
		return buffer;
	}

	operator VmaAllocation() const
	{
		return allocation;
	}

	vk::raii::Buffer* operator->()
	{
		return &buffer;
	}

	const vk::raii::Buffer* operator->() const
	{
		return &buffer;
	}

	buffer_allocation() = default;
	buffer_allocation(const vk::BufferCreateInfo& buffer_info, const VmaAllocationCreateInfo& alloc_info);
	buffer_allocation(const buffer_allocation&) = delete;
	buffer_allocation(buffer_allocation&&);
	const buffer_allocation& operator=(const buffer_allocation&) = delete;
	const buffer_allocation& operator=(buffer_allocation&&);
	~buffer_allocation();

	void * map();
};

class image_allocation
{
	VmaAllocation allocation = nullptr;
	vk::raii::Image image = nullptr;
	void * data = nullptr;

public:
	operator VkImage() const
	{
		return *image;
	}

	operator vk::Image() const
	{
		return *image;
	}

	operator const vk::raii::Image&() const
	{
		return image;
	}

	operator vk::raii::Image&()
	{
		return image;
	}

	operator VmaAllocation() const
	{
		return allocation;
	}

	vk::raii::Image* operator->()
	{
		return &image;
	}

	const vk::raii::Image* operator->() const
	{
		return &image;
	}

	image_allocation() = default;
	image_allocation(const vk::ImageCreateInfo& image_info, const VmaAllocationCreateInfo& alloc_info);
	image_allocation(const image_allocation&) = delete;
	image_allocation(image_allocation&&);
	const image_allocation& operator=(const image_allocation&) = delete;
	const image_allocation& operator=(image_allocation&&);
	~image_allocation();

	void * map();
};
