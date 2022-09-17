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

#include "pipeline.h"

#include "vk.h"

vk::pipeline::pipeline(VkDevice device, vk::pipeline::graphics_info & create_info, const VkPipelineLayout layout) :
        device(device)
{
	for (VkPipelineShaderStageCreateInfo & i: create_info.shader_stages)
	{
		i.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	}

	create_info.InputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	create_info.RasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	create_info.MultisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	create_info.DepthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	create_info.ColorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	VkPipelineVertexInputStateCreateInfo VertexInputState{
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	        .vertexBindingDescriptionCount = (uint32_t)create_info.vertex_input_bindings.size(),
	        .pVertexBindingDescriptions = create_info.vertex_input_bindings.data(),
	        .vertexAttributeDescriptionCount = (uint32_t)create_info.vertex_input_attributes.size(),
	        .pVertexAttributeDescriptions = create_info.vertex_input_attributes.data(),
	};

	VkPipelineDynamicStateCreateInfo DynamicState{
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	        .dynamicStateCount = (uint32_t)create_info.dynamic_states.size(),
	        .pDynamicStates = create_info.dynamic_states.data()};

	VkPipelineViewportStateCreateInfo ViewportState{
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	        .viewportCount = (uint32_t)create_info.viewports.size(),
	        .pViewports = create_info.viewports.data(),
	        .scissorCount = (uint32_t)create_info.scissors.size(),
	        .pScissors = create_info.scissors.data(),
	};

	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.stageCount = create_info.shader_stages.size();
	info.pStages = create_info.shader_stages.data();
	info.pVertexInputState = &VertexInputState;
	info.pInputAssemblyState = &create_info.InputAssemblyState;
	info.pViewportState = &ViewportState;
	info.pRasterizationState = &create_info.RasterizationState;
	info.pMultisampleState = &create_info.MultisampleState;
	info.pColorBlendState = &create_info.ColorBlendState;
	info.layout = layout;
	info.renderPass = create_info.renderPass;
	info.subpass = create_info.subpass;

	if (DynamicState.dynamicStateCount > 0)
		info.pDynamicState = &DynamicState;

	CHECK_VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &id));
}

vk::pipeline::~pipeline()
{
	if (device)
		vkDestroyPipeline(device, id, nullptr);
}
