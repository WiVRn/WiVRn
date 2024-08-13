/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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
#include <span>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class yuv_converter
{
public:
	image_allocation luma;
	image_allocation chroma;

private:
	vk::Device device;
	vk::Extent2D extent;
	std::vector<vk::Image> rgb;
	std::vector<vk::raii::ImageView> views_rgb;
	vk::raii::ImageView view_luma = nullptr;
	vk::raii::ImageView view_chroma = nullptr;

	vk::raii::DescriptorSetLayout ds_layout = nullptr;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::Pipeline pipeline = nullptr;
	vk::raii::DescriptorPool dp = nullptr;
	vk::DescriptorSet ds = nullptr;

public:
	yuv_converter();
	yuv_converter(vk::PhysicalDevice, vk::raii::Device & device, std::span<vk::Image> rgb, vk::Format format, vk::Extent2D extent);

	// Converts the given image to yuv, stored in luma and chroma images.
	// The output images will be in transfer src optimal layout
	void record_draw_commands(vk::raii::CommandBuffer & cmd_buf, size_t index);

	void assemble_planes(vk::Rect2D, vk::raii::CommandBuffer &, vk::Image target);
};
