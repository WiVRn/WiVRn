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

#include "yuv_converter.h"
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

static const VkFormat y_format = VK_FORMAT_R8_UNORM;
static const VkFormat uv_format = VK_FORMAT_R8G8_UNORM;

static int bytes_per_pixel(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R8_UNORM:
			return 1;
		case VK_FORMAT_R8G8_UNORM:
			return 2;
		default:
			assert(false);
			return 0;
	}
}

static VkResult create_image(vk_bundle & vk, VkExtent2D extent, VkFormat format, YuvConverter::image_bundle & bundle)
{
	VkResult res = vk_create_image_simple(&vk, extent, format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &bundle.image_memory, &bundle.image);
	vk_check_error("vk_create_image_simple", res, res);

	bundle.stride = extent.width * bytes_per_pixel(format);

	if (!vk_buffer_init(&vk, extent.height * bundle.stride, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &bundle.buffer, &bundle.buffer_memory))
	{
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	res = vk.vkMapMemory(vk.device, bundle.buffer_memory, 0, VK_WHOLE_SIZE, 0, &bundle.mapped_memory);
	vk_check_error("vkMapMemory", res, res);

	VkImageSubresourceRange subresource_range{};
	subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource_range.baseMipLevel = 0;
	subresource_range.levelCount = 1;
	subresource_range.baseArrayLayer = 0;
	subresource_range.layerCount = 1;

	res = vk_create_view(&vk, bundle.image, VK_IMAGE_VIEW_TYPE_2D, format, subresource_range, &bundle.view);
	vk_check_error("vk_create_view", res, res);
	return VK_SUCCESS;
}

static VkResult shader_load(struct vk_bundle * vk, const std::vector<uint32_t> & code, VkShaderModule * out_module)
{
	VkResult ret;

	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = code.size();
	info.pCode = code.data();

	VkShaderModule module;
	ret = vk->vkCreateShaderModule(vk->device, //
	                               &info,      //
	                               NULL,       //
	                               &module);   //
	if (ret != VK_SUCCESS)
	{
		VK_ERROR(vk, "vkCreateShaderModule failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_module = module;

	return VK_SUCCESS;
}

static void destroy_all(YuvConverter * ptr)
{
	auto & vk = ptr->vk;
	vk.vkDestroyDescriptorPool(vk.device, ptr->descriptor_pool, nullptr);
	vk.vkDestroyDescriptorSetLayout(vk.device, ptr->descriptor_set_layout, nullptr);
	for (auto & comp: {ptr->y, ptr->uv})
	{
		vk.vkDestroyPipeline(vk.device, comp.pipeline, nullptr);
		vk.vkDestroyFramebuffer(vk.device, comp.frame_buffer, nullptr);
		vk.vkDestroyRenderPass(vk.device, comp.render_pass, nullptr);
		vk.vkDestroyShaderModule(vk.device, comp.frag, nullptr);

		vk.vkUnmapMemory(vk.device, comp.buffer_memory);
		vk.vkDestroyBuffer(vk.device, comp.buffer, nullptr);
		vk.vkFreeMemory(vk.device, comp.buffer_memory, nullptr);
		vk.vkDestroyImageView(vk.device, comp.view, nullptr);
		vk.vkDestroyImage(vk.device, comp.image, nullptr);
		vk.vkFreeMemory(vk.device, comp.image_memory, nullptr);
	}
	vk.vkDestroyPipelineLayout(vk.device, ptr->pipeline_layout, nullptr);

	vk.vkDestroyShaderModule(vk.device, ptr->vert, nullptr);
}

#define vk_check_throw(fun, res)                                \
	do                                                      \
	{                                                       \
		if (vk_has_error(res, fun, __FILE__, __LINE__)) \
			throw std::runtime_error(fun "failed"); \
	} while (0)

YuvConverter::YuvConverter(vk_bundle * vk, VkExtent3D extent, int offset_x, int offset_y, int input_width, int input_height) :
        vk(*vk)
{
	std::unique_ptr<YuvConverter, decltype(&destroy_all)> deleter(this, destroy_all);

	y.extent = {extent.width, extent.height};
	uv.extent = {extent.width / 2, extent.height / 2};

	assert(y_format != VK_FORMAT_UNDEFINED);
	assert(uv_format != VK_FORMAT_UNDEFINED);

	VkResult res = create_image(*vk, y.extent, y_format, y);
	vk_check_throw("create_image (y)", res);

	res = create_image(*vk, uv.extent, uv_format, uv);
	vk_check_throw("create_image (uv)", res);

	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &samplerLayoutBinding;

	res = vk->vkCreateDescriptorSetLayout(vk->device, &layoutInfo, nullptr, &descriptor_set_layout);
	vk_check_throw("vkCreateDescriptorSetLayout", res);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.pSetLayouts = &descriptor_set_layout;
	pipelineLayoutInfo.setLayoutCount = 1;
	res = vk->vkCreatePipelineLayout(vk->device, &pipelineLayoutInfo, nullptr, &pipeline_layout);
	vk_check_throw("vkCreatePipelineLayout", res);

	res = shader_load(vk, shaders.at("yuv_converter.vert"), &vert);
	vk_check_throw("shader_load", res);

	res = shader_load(vk, shaders.at("yuv_converter.y.frag"), &y.frag);
	vk_check_throw("shader_load", res);
	res =
	        shader_load(vk, shaders.at("yuv_converter.uv.frag"), &uv.frag);
	vk_check_throw("shader_load", res);

	for (int i = 0; i < 2; ++i)
	{
		VkAttachmentDescription colorAttachment{};
		VkAttachmentReference colorAttachmentRef{};
		auto & comp = (i == 0) ? y : uv;
		double scale = (i == 0) ? 1.0 : 0.5;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorAttachment.format = (i == 0) ? y_format : uv_format;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		res = vk->vkCreateRenderPass(vk->device, &renderPassInfo, nullptr, &comp.render_pass);
		vk_check_throw("vkCreateRenderPass", res);

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = comp.render_pass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &comp.view;
		framebufferInfo.width = comp.extent.width;
		framebufferInfo.height = comp.extent.height;
		framebufferInfo.layers = 1;

		res = vk->vkCreateFramebuffer(vk->device, &framebufferInfo, nullptr, &comp.frame_buffer);
		vk_check_throw("vkCreateFramebuffer", res);

		VkPipelineShaderStageCreateInfo shaderStages[2] = {};

		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vert;
		shaderStages[0].pName = "main";

		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = comp.frag;
		shaderStages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport{};
		viewport.x = -offset_x * scale;
		viewport.y = -offset_y * scale;
		viewport.width = input_width * scale;
		viewport.height = input_height * scale;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = comp.extent;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = std::size(shaderStages);
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipeline_layout;
		pipelineInfo.renderPass = comp.render_pass;
		pipelineInfo.subpass = 0;

		res = vk->vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &comp.pipeline);
		vk_check_throw("vkCreateGraphicsPipelines", res);
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.anisotropyEnable = VK_FALSE;
	res = vk->vkCreateSampler(vk->device, &samplerInfo, nullptr, &sampler);
	vk_check_throw("vkCreateSampler", res);

	(void)deleter.release();
}

YuvConverter::~YuvConverter()
{
	destroy_all(this);
}

void YuvConverter::SetImages(int num_images, VkImage * images, VkImageView * views)
{
	// os_mutex_lock(&vk.cmd_pool_mutex);
	assert(command_buffers.empty());
	assert(descriptor_sets.empty());
	descriptor_sets.resize(num_images);
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = num_images;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = num_images;
	VkResult res = vk.vkCreateDescriptorPool(vk.device, &poolInfo, nullptr, &descriptor_pool);
	vk_check_throw("vkCreateDescriptorPool", res);
	for (int i = 0; i < num_images; ++i)
	{
		const auto & view = views[i];
		auto & descriptor_set = descriptor_sets[i];

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptor_pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptor_set_layout;
		res = vk.vkAllocateDescriptorSets(vk.device, &allocInfo, &descriptor_set);
		vk_check_throw("vkAllocateDescriptorSets", res);

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageInfo.imageView = view;
		imageInfo.sampler = sampler;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = descriptor_set;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vk.vkUpdateDescriptorSets(vk.device, 1, &descriptorWrite, 0, nullptr);

		VkCommandBuffer cmdBuffer;
		res = vk_cmd_buffer_create_and_begin(&vk, &cmdBuffer);
		vk_check_throw("vk_cmd_buffer_create_and_begin", res);

		for (auto & comp: {y, uv})
		{
			VkRenderPassBeginInfo render_pass_info{};
			render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			render_pass_info.renderPass = comp.render_pass;
			render_pass_info.framebuffer = comp.frame_buffer;
			render_pass_info.renderArea.extent = comp.extent;

			vk.vkCmdBeginRenderPass(cmdBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
			vk.vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, comp.pipeline);
			vk.vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
			vk.vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
			vk.vkCmdEndRenderPass(cmdBuffer);

			if (comp.buffer != VK_NULL_HANDLE)
			{
				VkBufferImageCopy copy{};
				copy.bufferOffset = 0;
				copy.bufferRowLength = 0;
				copy.bufferImageHeight = 0;
				copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copy.imageSubresource.mipLevel = 0;
				copy.imageSubresource.baseArrayLayer = 0;
				copy.imageSubresource.layerCount = 1;
				copy.imageOffset = {0, 0, 0};
				copy.imageExtent = {comp.extent.width, comp.extent.height, 1};
				// TODO synchronisation?
				vk.vkCmdCopyImageToBuffer(cmdBuffer, comp.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, comp.buffer, 1, &copy);
			}
		}

		res = vk.vkEndCommandBuffer(cmdBuffer);
		vk_check_throw("vkEndCommandBuffer", res);

		command_buffers.push_back(cmdBuffer);
	}
	// os_mutex_unlock(&vk.cmd_pool_mutex);
}
