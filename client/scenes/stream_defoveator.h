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

#include "blitter.h"
#include "vk/allocation.h"
#include "wivrn_packets.h"
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

class stream_defoveator
{
	struct vertex;
	static const uint32_t view_count = 2;
	// Vertex buffer
	buffer_allocation buffer;
	size_t vertices_size = 0;

	vk::raii::Device & device;
	vk::raii::PhysicalDevice & physical_device;

	// Graphic pipeline
	vk::raii::RenderPass renderpass = nullptr;
	vk::raii::DescriptorPool ds_pool = nullptr;
	struct pipeline_t
	{
		vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
		vk::DescriptorSet ds;
		vk::raii::PipelineLayout layout = nullptr;
		vk::raii::Pipeline pipeline = nullptr;
	};
	pipeline_t pipeline_rgb[view_count];
	pipeline_t pipeline_a[view_count];

	// Allowed sizes for variable shading rate
	// indices are for x, y
	// 0 is 1 pixel
	// 1 is 2 or 3 pixels
	// 2 is 4 pixels or more
	uint32_t fragment_sizes[3][3] = {};

	// Destination images
	std::vector<vk::Image> output_images;
	std::vector<vk::raii::ImageView> output_image_views;
	std::vector<vk::raii::Framebuffer> framebuffers;
	vk::Extent2D output_extent;

	void ensure_vertices(size_t num_vertices);
	vertex * get_vertices(size_t view);

	uint32_t shading_rate(int pixels_x, int pixels_y);
	pipeline_t & ensure_pipeline(size_t view, vk::Sampler rgb, vk::Sampler a);

public:
	stream_defoveator(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        std::vector<vk::Image> output_images,
	        vk::Extent2D output_extent,
	        vk::Format format);

	stream_defoveator(const stream_defoveator &) = delete;

	void defoveate(
	        vk::raii::CommandBuffer & command_buffer,
	        const std::array<wivrn::to_headset::foveation_parameter, 2> & foveation,
	        std::span<wivrn::blitter::output> inputs,
	        int destination);

	XrExtent2Di defoveated_size(const wivrn::to_headset::foveation_parameter &) const;
};
