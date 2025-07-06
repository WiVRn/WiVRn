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
#include "wivrn_packets.h"
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

class stream_reprojection
{
	struct vertex;
	const uint32_t view_count;
	// Vertex buffer
	buffer_allocation buffer;
	size_t vertices_size = 0;

	vk::raii::Device & device;

	// Graphic pipeline
	vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
	vk::raii::DescriptorPool descriptor_pool = nullptr;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::RenderPass renderpass = nullptr;
	vk::raii::Pipeline pipeline = nullptr;

	// Source image
	vk::raii::Sampler sampler = nullptr;
	vk::Image input_image;
	std::vector<vk::raii::ImageView> input_image_views;
	std::vector<vk::DescriptorSet> descriptor_sets;
	vk::Extent2D input_extent;

	// Destination images
	std::vector<vk::Image> output_images;
	std::vector<vk::raii::ImageView> output_image_views;
	std::vector<vk::raii::Framebuffer> framebuffers;
	vk::Extent2D output_extent;

	void ensure_vertices(size_t num_vertices);
	vertex * get_vertices(size_t view);

public:
	stream_reprojection(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        vk::Image input_image,
	        vk::Extent2D input_extent,
	        uint32_t view_count,
	        std::vector<vk::Image> output_images,
	        vk::Extent2D output_extent,
	        vk::Format format);

	stream_reprojection(const stream_reprojection &) = delete;

	void reproject(
	        vk::raii::CommandBuffer & command_buffer,
	        const std::array<wivrn::to_headset::foveation_parameter, 2> & foveation,
	        int destination);

	XrExtent2Di defoveated_size(const wivrn::to_headset::foveation_parameter &) const;
};
