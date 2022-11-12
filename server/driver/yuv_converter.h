// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

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
		void *mapped_memory = nullptr;
		VkRenderPass render_pass = VK_NULL_HANDLE;
		VkFramebuffer frame_buffer = VK_NULL_HANDLE;
		VkShaderModule frag = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkExtent2D extent;
		int stride;
	};
	vk_bundle &vk;
	image_bundle y;
	image_bundle uv;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	VkShaderModule vert = VK_NULL_HANDLE;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

	std::vector<VkCommandBuffer> command_buffers;
	std::vector<VkDescriptorSet> descriptor_sets;

	YuvConverter(vk_bundle *vk, VkExtent3D extent, int offset_x, int offset_y, int input_width, int input_height);
	~YuvConverter();

	void
	SetImages(int num_images, VkImage *images, VkImageView *views);
};
