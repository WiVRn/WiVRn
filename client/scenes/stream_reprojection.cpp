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

#include "stream_reprojection.h"
#include "spdlog/spdlog.h"
#include "utils/check.h"
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vulkan/vulkan_core.h>

struct stream_reprojection::uniform
{
	alignas(16) glm::mat4 reprojection;
};

stream_reprojection::~stream_reprojection()
{
	cleanup();
}

void stream_reprojection::cleanup()
{
	if (device)
	{
		if (descriptor_pool)
			vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

		if (descriptor_set_layout)
			vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);

		for (VkFramebuffer framebuffer: framebuffers)
			vkDestroyFramebuffer(device, framebuffer, nullptr);

		for (VkImageView image_view: output_image_views)
			vkDestroyImageView(device, image_view, nullptr);

		for (VkImageView image_view: input_image_views)
			vkDestroyImageView(device, image_view, nullptr);

		if (sampler)
			vkDestroySampler(device, sampler, nullptr);

		descriptor_pool = VK_NULL_HANDLE;
		descriptor_set_layout = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
		framebuffers.clear();
		output_image_views.clear();
		input_image_views.clear();
		ubo.clear();
	}
}

void stream_reprojection::init(VkDevice device, VkPhysicalDevice physical_device, std::vector<VkImage> input_images_, std::vector<VkImage> output_images_, VkExtent2D extent, VkFormat format)
{
	cleanup();
	this->device = device;
	this->physical_device = physical_device,
	this->input_images = std::move(input_images_);
	this->output_images = std::move(output_images_);
	this->extent = extent;
	this->format = format;

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(physical_device, &properties);

	VkSamplerCreateInfo sampler_info{
	        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	        .magFilter = VK_FILTER_LINEAR,
	        .minFilter = VK_FILTER_LINEAR,
	        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .mipLodBias = 0.0f,
	        .anisotropyEnable = VK_FALSE,
	        .maxAnisotropy = 1,
	        .compareEnable = VK_FALSE,
	        .compareOp = VK_COMPARE_OP_NEVER,
	        .minLod = 0.0f,
	        .maxLod = 0.0f,
	        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	        .unnormalizedCoordinates = VK_FALSE,
	};

	CHECK_VK(vkCreateSampler(device, &sampler_info, nullptr, &sampler));

	size_t uniform_size = sizeof(uniform) + properties.limits.minUniformBufferOffsetAlignment - 1;
	uniform_size = uniform_size - uniform_size % properties.limits.minUniformBufferOffsetAlignment;

	VkBufferCreateInfo create_info{
	        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	        .size = uniform_size * input_images.size(),
	        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	buffer = vk::buffer(device, create_info);
	memory = vk::device_memory(device, physical_device, buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	memory.map_memory();
	for (size_t i = 0; i < input_images.size(); i++)
		ubo.push_back(reinterpret_cast<uniform *>(reinterpret_cast<uintptr_t>(memory.data()) + i * uniform_size));

	// Create VkDescriptorSetLayout
	std::array<VkDescriptorSetLayoutBinding, 2> layout_binding{
	        VkDescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	                .descriptorCount = 1,
	                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	        },
	        {
	                .binding = 1,
	                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	                .descriptorCount = 1,
	                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	        }};

	VkDescriptorSetLayoutCreateInfo layout_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	        .bindingCount = layout_binding.size(),
	        .pBindings = layout_binding.data(),
	};

	CHECK_VK(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout));

	std::array<VkDescriptorPoolSize, 2> pool_size{VkDescriptorPoolSize{
	                                                      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	                                                      .descriptorCount = (uint32_t)input_images.size(),
	                                              },
	                                              VkDescriptorPoolSize{
	                                                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	                                                      .descriptorCount = (uint32_t)input_images.size(),
	                                              }};

	VkDescriptorPoolCreateInfo pool_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	        .flags = 0,
	        .maxSets = (uint32_t)input_images.size(),
	        .poolSizeCount = pool_size.size(),
	        .pPoolSizes = pool_size.data(),
	};

	CHECK_VK(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool));

	// Create image views and descriptor sets
	input_image_views.reserve(input_images.size());
	descriptor_sets.reserve(input_images.size());
	VkDeviceSize offset = 0;
	for (VkImage image: input_images)
	{
		VkImageViewCreateInfo iv_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		        .image = image,
		        .viewType = VK_IMAGE_VIEW_TYPE_2D,
		        .format = VK_FORMAT_A8B8G8R8_SRGB_PACK32,
		        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
		        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = 1}};

		VkImageView image_view;
		CHECK_VK(vkCreateImageView(device, &iv_info, nullptr, &image_view));
		input_image_views.push_back(image_view);

		VkDescriptorSetAllocateInfo ds_info{
		        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		        .descriptorPool = descriptor_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &descriptor_set_layout,
		};
		VkDescriptorSet ds;
		CHECK_VK(vkAllocateDescriptorSets(device, &ds_info, &ds));
		descriptor_sets.push_back(ds);

		VkDescriptorImageInfo image_info{
		        .sampler = sampler,
		        .imageView = image_view,
		        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorBufferInfo buffer_info{
		        .buffer = buffer, .offset = offset, .range = sizeof(uniform)};
		offset += uniform_size;

		std::array<VkWriteDescriptorSet, 2> write{
		        VkWriteDescriptorSet{
		                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		                .dstSet = ds,
		                .dstBinding = 0,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		                .pImageInfo = &image_info,
		        },
		        VkWriteDescriptorSet{
		                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		                .dstSet = ds,
		                .dstBinding = 1,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		                .pBufferInfo = &buffer_info},
		};

		vkUpdateDescriptorSets(device, write.size(), write.data(), 0, nullptr);
	}

	// Create renderpass
	VkAttachmentReference color_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	vk::renderpass::info renderpass_info{.attachments = {VkAttachmentDescription{
	                                             .format = format,
	                                             .samples = VK_SAMPLE_COUNT_1_BIT,
	                                             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	                                             .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	                                             .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                             .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                     }},
	                                     .subpasses = {VkSubpassDescription{
	                                             .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                             .colorAttachmentCount = 1,
	                                             .pColorAttachments = &color_ref,
	                                     }},
	                                     .dependencies = {}};

	renderpass = vk::renderpass(device, renderpass_info);

	// Create graphics pipeline
	vk::shader vertex_shader(device, "reprojection.vert");
	vk::shader fragment_shader(device, "reprojection.frag");

	VkPipelineColorBlendAttachmentState pcbas{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
	                                                            VK_COLOR_COMPONENT_G_BIT |
	                                                            VK_COLOR_COMPONENT_B_BIT};

	layout = vk::pipeline_layout(device, {.descriptor_set_layouts = {descriptor_set_layout}});

	vk::pipeline::graphics_info pipeline_info{
	        .shader_stages =
	                {{.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main"},
	                 {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main"}},
	        .vertex_input_bindings = {},
	        .vertex_input_attributes = {},
	        .InputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
	        .viewports = {VkViewport{.x = 0,
	                                 .y = 0,
	                                 .width = (float)extent.width,
	                                 .height = (float)extent.height,
	                                 .minDepth = 0,
	                                 .maxDepth = 1}},
	        .scissors = {VkRect2D{
	                .offset = {0, 0},
	                .extent = extent,
	        }},
	        .RasterizationState = {.polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1},
	        .MultisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
	        .ColorBlendState = {.attachmentCount = 1, .pAttachments = &pcbas},
	        .dynamic_states = {},
	        .renderPass = renderpass,
	        .subpass = 0,
	};

	pipeline = vk::pipeline(device, pipeline_info, layout);

	// Create image views and framebuffers
	output_image_views.reserve(output_images.size());
	framebuffers.reserve(output_images.size());
	for (VkImage image: output_images)
	{
		VkImageViewCreateInfo iv_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		        .image = image,
		        .viewType = VK_IMAGE_VIEW_TYPE_2D,
		        .format = format,
		        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
		        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = 1}};

		VkImageView image_view;
		CHECK_VK(vkCreateImageView(device, &iv_info, nullptr, &image_view));
		output_image_views.push_back(image_view);

		VkFramebufferCreateInfo fb_create_info{
		        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		        .renderPass = renderpass,
		        .attachmentCount = 1,
		        .pAttachments = &image_view,
		        .width = extent.width,
		        .height = extent.height,
		        .layers = 1,
		};

		VkFramebuffer framebuffer;
		CHECK_VK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &framebuffer));
		framebuffers.push_back(framebuffer);
	}
}

void stream_reprojection::reproject(VkCommandBuffer command_buffer, int source, int destination, XrQuaternionf source_pose, XrFovf source_fov, XrQuaternionf dest_pose, XrFovf dest_fov)
{
	if (source < 0 || source >= (int)input_images.size())
		throw std::runtime_error("Invalid source image index");
	if (destination < 0 || destination >= (int)output_images.size())
		throw std::runtime_error("Invalid destination image index");

	// Compute the reprojection matrix
	float zn = 1;
	float r = tan(dest_fov.angleRight);
	float l = tan(dest_fov.angleLeft);
	float t = tan(dest_fov.angleUp);
	float b = tan(dest_fov.angleDown);

	// clang-format off
	glm::mat4 hmd_proj{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t), -1,   -1 },
		{ 0,           0,           -2*zn, 0 }
	};

	glm::mat4 hmd_unview = glm::mat4_cast(glm::quat(
		dest_pose.w,
		dest_pose.x,
		dest_pose.y,
		dest_pose.z));

	r = tan(source_fov.angleRight);
	l = tan(source_fov.angleLeft);
	t = tan(source_fov.angleUp);
	b = tan(source_fov.angleDown);

	glm::mat4 video_proj{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t), -1,   -1 },
		{ 0,           0,           -2*zn, 0 }
	};

	glm::mat4 video_view = glm::mat4_cast(glm::quat(
		-source_pose.w,
		source_pose.x,
		source_pose.y,
		source_pose.z));
	// clang-format on

	ubo[source]->reprojection = video_proj * video_view * hmd_unview * glm::inverse(hmd_proj);

	VkClearValue clear_color{};

	VkRenderPassBeginInfo begin_info{
	        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
	        .renderPass = renderpass,
	        .framebuffer = framebuffers[destination],
	        .renderArea =
	                {
	                        .offset = {0, 0},
	                        .extent = extent,
	                },
	        .clearValueCount = 1,
	        .pClearValues = &clear_color,
	};

	vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor_sets[source], 0, nullptr);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(command_buffer);
}
