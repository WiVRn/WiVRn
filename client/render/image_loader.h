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

struct image_loader
{
	image_allocation image;
	vk::raii::ImageView image_view = nullptr;

	vk::Format format;
	vk::Extent3D extent;
	vk::ImageType image_type;
	vk::ImageViewType image_view_type;

	uint32_t num_mipmaps;

	buffer_allocation staging_buffer;

	// Load a PNG/JPEG file
	image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, std::span<const std::byte> bytes, bool srgb);

	// Load raw pixel data
	image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, const void * pixels, vk::Extent3D extent, vk::Format format);

	template <typename T>
	image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, std::span<T> pixels, vk::Extent3D extent, vk::Format format) :
	        image_loader(device, cb, pixels.data(), extent, format)
	{}

	template <typename T, size_t N>
	image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, const std::array<T, N> & pixels, vk::Extent3D extent, vk::Format format) :
	        image_loader(device, cb, pixels.data(), extent, format)
	{}

private:
	void do_load(vk::raii::Device & device, vk::raii::CommandBuffer & cb, const void * pixels, vk::Extent3D extent, vk::Format format);
};
