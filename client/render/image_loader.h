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
#include <cstddef>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <ktxvulkan.h>
#include <span>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

struct loaded_image
{
	image_allocation image;
	vk::raii::ImageView image_view;

	vk::Format format;
	vk::Extent3D extent;
	uint32_t num_mipmaps;
	vk::ImageViewType image_view_type;
	bool is_alpha_premultiplied;
};

struct image_loader
{
	image_loader(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, thread_safe<vk::raii::Queue> & queue, uint32_t queue_family_index);
	image_loader(const image_loader &) = delete;
	image_loader & operator=(const image_loader &) = delete;
	~image_loader();

	// Load a PNG/JPEG/KTX2 file
	loaded_image load(std::span<const std::byte> bytes, bool srgb, const std::string & name = "", bool premultiply = false, const std::filesystem::path & output_file = "");

	// Load raw pixel data
	loaded_image load(const void * pixels, size_t size, vk::Extent3D extent, vk::Format format, const std::string & name = "", bool premultiply = false);

	template <typename T>
	loaded_image load(std::span<T> pixels, vk::Extent3D extent, vk::Format format, const std::string & name = "", bool premultiply = false)
	{
		return load(pixels.data(), pixels.size() * sizeof(T), extent, format, name);
	}

	template <typename T, size_t N>
	loaded_image load(const std::array<T, N> & pixels, vk::Extent3D extent, vk::Format format, const std::string & name = "", bool premultiply = false)
	{
		return load(pixels.data(), pixels.size() * sizeof(T), extent, format, name);
	}

	template <typename T>
	loaded_image load(const std::vector<T> & pixels, vk::Extent3D extent, vk::Format format, const std::string & name = "", bool premultiply = false)
	{
		return load(pixels.data(), pixels.size() * sizeof(T), extent, format, name);
	}

	std::shared_ptr<loaded_image> operator()(std::span<const std::byte> bytes, bool srgb, const std::string & name = "", bool premultiply = false)
	{
		return std::make_shared<loaded_image>(load(bytes, srgb, name, premultiply));
	}

private:
	ktxVulkanDeviceInfo vdi;
	vk::raii::Device & device;
	thread_safe<vk::raii::Queue> & queue;
	vk::raii::CommandPool cb_pool;

	std::vector<std::pair<vk::Format, ktx_transcode_fmt_e>> supported_srgb_formats;
	std::vector<std::pair<vk::Format, ktx_transcode_fmt_e>> supported_linear_formats;

	buffer_allocation staging_buffer;

	loaded_image do_load_raw(const void * pixels, vk::Extent3D extent, vk::Format format, const std::string & name, bool premultiply);

	loaded_image do_load_image(std::span<const std::byte> bytes, bool srgb, const std::string & name, bool premultiply);
	loaded_image do_load_ktx(std::span<const std::byte> bytes, bool srgb, const std::string & name, const std::filesystem::path & output_file = "");
};
