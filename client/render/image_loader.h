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

#include "vk/allocation.h"
#include <cstddef>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <tuple>
#include <vulkan/vulkan_raii.hpp>

struct ktxVulkanDeviceInfo;
struct ktxVulkanTexture;

struct image_loader
{
	std::shared_ptr<vk::Image> image;
	vk::raii::ImageView image_view = nullptr;

	vk::Format format;
	vk::Extent3D extent;
	vk::ImageViewType image_view_type;

	uint32_t num_mipmaps;

	image_loader(vk::raii::PhysicalDevice physical_device, vk::raii::Device & device, vk::raii::Queue & queue, vk::raii::CommandPool & cb_pool);

	// Load a PNG/JPEG/KTX2 file
	void load(std::span<const std::byte> bytes, bool srgb);

	// Load raw pixel data
	void load(const void * pixels, size_t size, vk::Extent3D extent, vk::Format format);


	template<typename T>
	void load(std::span<T> pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

	template<typename T, size_t N>
	void load(const std::array<T, N> & pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

	template<typename T>
	void load(const std::vector<T> & pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

	~image_loader();

private:
	ktxVulkanDeviceInfo * vdi = nullptr;

	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	vk::raii::Queue & queue;
	vk::raii::CommandPool & cb_pool;

	buffer_allocation staging_buffer;

	void do_load_raw(const void * pixels, vk::Extent3D extent, vk::Format format);

	void do_load_ktx(std::span<const std::byte> bytes);

	void create_image_view();
};
