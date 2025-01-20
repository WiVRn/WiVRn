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
	const uint32_t view_count;
	// Uniform buffer
	buffer_allocation buffer;
	size_t uniform_size;

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

	// Destination images
	std::vector<vk::Image> output_images;
	std::vector<vk::raii::ImageView> output_image_views;
	std::vector<vk::raii::Framebuffer> framebuffers;
	vk::Extent2D extent;

	// Foveation
	std::array<wivrn::to_headset::foveation_parameter, 2> foveation_parameters;

public:
	stream_reprojection(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        vk::Image input_image,
	        uint32_t view_count,
	        std::vector<vk::Image> output_images,
	        vk::Extent2D extent,
	        vk::Format format,
	        const wivrn::to_headset::video_stream_description & description);

	stream_reprojection(const stream_reprojection &) = delete;

	void reproject(
	        vk::raii::CommandBuffer & command_buffer,
	        int destination);

	void set_foveation(std::array<wivrn::to_headset::foveation_parameter, 2> foveation);
};
