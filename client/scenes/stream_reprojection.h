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

#include "vk/buffer.h"
#include "vk/device_memory.h"
#include "vk/pipeline.h"
#include "vk/pipeline_layout.h"
#include "vk/renderpass.h"
#include "vk/shader.h"
#include "wivrn_packets.h"
#include <vulkan/vulkan_core.h>
#include <openxr/openxr.h>

class stream_reprojection
{
	struct uniform;

	VkDevice device;
	VkPhysicalDevice physical_device;

	// Uniform buffer
	vk::buffer buffer;
	vk::device_memory memory;
	std::vector<uniform *> ubo{};

	// Source images
	VkSampler sampler{};
	std::vector<VkImage> input_images;
	std::vector<VkImageView> input_image_views;
	std::vector<VkDescriptorSet> descriptor_sets;

	// Destination images
	std::vector<VkImage> output_images;
	std::vector<VkImageView> output_image_views;
	std::vector<VkFramebuffer> framebuffers;
	VkExtent2D extent{};
	VkFormat format{};

	// Graphic pipeline
	VkDescriptorSetLayout descriptor_set_layout{};
	VkDescriptorPool descriptor_pool{};
	vk::pipeline_layout layout;
	vk::pipeline pipeline;
	vk::renderpass renderpass;

	// Foveation
	std::array<xrt::drivers::wivrn::to_headset::video_stream_description::foveation_parameter, 2> foveation_parameters;

	void cleanup();

public:
	stream_reprojection() = default;
	stream_reprojection(const stream_reprojection &) = delete;
	void init(VkDevice device, VkPhysicalDevice physical_device, std::vector<VkImage> input_images, std::vector<VkImage> output_images, VkExtent2D extent, VkFormat format, const xrt::drivers::wivrn::to_headset::video_stream_description & description);

	~stream_reprojection();

	void reproject(VkCommandBuffer command_buffer, int source, int destination, XrQuaternionf source_pose, XrFovf source_fov, XrQuaternionf destination_pose, XrFovf destination_fov);
};
