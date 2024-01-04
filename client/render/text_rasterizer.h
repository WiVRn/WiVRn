/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <vulkan/vulkan_raii.hpp>

#include "vk/allocation.h"
#include <freetype/freetype.h>
#include <ft2build.h>
#include <hb.h>
#include <string_view>
#include <vector>

struct text
{
	vk::Extent2D size;

#ifndef TEST
	static inline const vk::Format format = vk::Format::eR8Unorm;
	static inline const vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	static inline const vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
	image_allocation image;

#else
	std::vector<uint8_t> bitmap;
#endif
};

class text_rasterizer
{
	vk::raii::Device & device;
	vk::raii::PhysicalDevice & physical_device;
	vk::raii::CommandPool & command_pool;
	vk::raii::Queue & queue;
	vk::raii::Fence fence;

	FT_Library freetype{};
	FT_Face face{};

	hb_font_t * font{};
	hb_buffer_t * buffer{};

	image_allocation create_image(vk::Extent2D size);
	buffer_allocation create_buffer(size_t size);
	vk::raii::DeviceMemory allocate_memory(vk::Buffer buffer, vk::MemoryPropertyFlags flags);

public:
	text_rasterizer(vk::raii::Device & device, vk::raii::PhysicalDevice & physical_device, vk::raii::CommandPool & command_pool, vk::raii::Queue & queue);
	~text_rasterizer();

	text render(std::string_view s);
};
