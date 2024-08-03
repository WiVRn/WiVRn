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
	struct uniform;

	// Uniform buffer
	buffer_allocation buffer;
	std::vector<uniform *> ubo;

	// Graphic pipeline
	vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
	vk::raii::DescriptorPool descriptor_pool = nullptr;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::RenderPass renderpass = nullptr;
	vk::raii::Pipeline pipeline = nullptr;

	// Source images
	vk::raii::Sampler sampler = nullptr;
	std::vector<vk::Image> input_images;
	std::vector<vk::raii::ImageView> input_image_views;
	std::vector<vk::DescriptorSet> descriptor_sets;

	// Destination images
	std::vector<vk::Image> output_images;
	std::vector<vk::raii::ImageView> output_image_views;
	std::vector<vk::raii::Framebuffer> framebuffers;
	vk::Extent2D extent;

	// Foveation
	std::array<xrt::drivers::wivrn::to_headset::foveation_parameter, 2> foveation_parameters;

public:
	stream_reprojection(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        std::vector<vk::Image> input_images,
	        std::vector<vk::Image> output_images,
	        vk::Extent2D extent,
	        vk::Format format,
	        const xrt::drivers::wivrn::to_headset::video_stream_description & description);

	stream_reprojection(const stream_reprojection &) = delete;

	void reproject(
	        vk::raii::CommandBuffer & command_buffer,
	        int source,
	        int destination);

	void set_foveation(std::array<xrt::drivers::wivrn::to_headset::foveation_parameter, 2> foveation);
};
