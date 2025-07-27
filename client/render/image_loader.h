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

#include "utils/thread_safe.h"
#include "vk/allocation.h"
#include "vk/fwd.h"
#include "wivrn_config.h"
#include <cstddef>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <vulkan/vulkan.hpp>

#if WIVRN_USE_LIBKTX
#include <ktxvulkan.h>
#endif

struct image_loader
{
	vk::Image image;
	vk::Format format;
	vk::Extent3D extent;
	vk::ImageViewType image_view_type;

	std::shared_ptr<vk::raii::ImageView> image_view;

	uint32_t num_mipmaps;

	image_loader(vk::raii::PhysicalDevice physical_device, vk::raii::Device & device, thread_safe<vk::raii::Queue> & queue, vk::raii::CommandPool & cb_pool);
	image_loader(const image_loader &) = delete;
	image_loader & operator=(const image_loader &) = delete;
	~image_loader();

	// Load a PNG/JPEG/KTX2 file
	void load(std::span<const std::byte> bytes, bool srgb);

	// Load raw pixel data
	void load(const void * pixels, size_t size, vk::Extent3D extent, vk::Format format);

	template <typename T>
	void load(std::span<T> pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

	template <typename T, size_t N>
	void load(const std::array<T, N> & pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

	template <typename T>
	void load(const std::vector<T> & pixels, vk::Extent3D extent, vk::Format format)
	{
		load(pixels.data(), pixels.size() * sizeof(T), extent, format);
	}

private:
#if WIVRN_USE_LIBKTX
	ktxVulkanDeviceInfo vdi;
#endif

	vk::raii::Device & device;
	thread_safe<vk::raii::Queue> & queue;
	vk::raii::CommandPool & cb_pool;

	buffer_allocation staging_buffer;

	void do_load_raw(const void * pixels, vk::Extent3D extent, vk::Format format);

	void do_load_ktx(std::span<const std::byte> bytes);

	void create_image_view();
};
