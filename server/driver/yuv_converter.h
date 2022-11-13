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

#include "vk/vk_helpers.h"
#include <vector>
#include <vulkan/vulkan_core.h>

class YuvConverter
{
public:
	struct image_bundle
	{
		VkDeviceMemory image_memory = VK_NULL_HANDLE;
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		void * mapped_memory = nullptr;
		VkRenderPass render_pass = VK_NULL_HANDLE;
		VkFramebuffer frame_buffer = VK_NULL_HANDLE;
		VkShaderModule frag = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkExtent2D extent;
		int stride;
	};
	vk_bundle & vk;
	image_bundle y;
	image_bundle uv;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	VkShaderModule vert = VK_NULL_HANDLE;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

	std::vector<VkCommandBuffer> command_buffers;
	std::vector<VkDescriptorSet> descriptor_sets;

	YuvConverter(vk_bundle * vk, VkExtent3D extent, int offset_x, int offset_y, int input_width, int input_height);
	~YuvConverter();

	void SetImages(int num_images, VkImage * images, VkImageView * views);
};
