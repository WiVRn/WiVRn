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

#ifndef TEST
#include "vk/device_memory.h"
#include "vk/image.h"
#endif

#include <vulkan/vulkan.h>

#include <freetype/freetype.h>
#include <ft2build.h>
#include <hb.h>
#include <string_view>
#include <vector>

struct text
{
#ifndef TEST
	static inline const VkFormat format = VK_FORMAT_R8_UNORM;
	static inline const VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	static inline const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	vk::image image;
	vk::device_memory memory;
#else
	std::vector<uint8_t> bitmap;
#endif

	VkExtent2D size;
};

class text_rasterizer
{
	VkDevice device{};
	VkPhysicalDevice physical_device{};
	VkCommandPool command_pool{};
	VkQueue queue{};
	VkFence fence{};

	FT_Library freetype{};
	FT_Face face{};

	hb_font_t * font{};
	hb_buffer_t * buffer{};

public:
	text_rasterizer(VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue);
	~text_rasterizer();

	text render(std::string_view s);
};
