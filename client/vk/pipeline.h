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

#include <optional>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace vk
{
class pipeline_builder
{
public:
	vk::PipelineCreateFlags flags = {};

	std::vector<vk::PipelineShaderStageCreateInfo> Stages;

	vk::PipelineVertexInputStateCreateInfo VertexInputState;
	std::vector<vk::VertexInputBindingDescription> VertexBindingDescriptions;
	std::vector<vk::VertexInputAttributeDescription> VertexAttributeDescriptions;

	std::optional<vk::PipelineInputAssemblyStateCreateInfo> InputAssemblyState;
	std::optional<vk::PipelineTessellationStateCreateInfo> TessellationState;

	vk::PipelineViewportStateCreateInfo ViewportState; // Automatically filled
	std::vector<vk::Viewport> Viewports = {};
	std::vector<vk::Rect2D> Scissors = {};

	std::optional<vk::PipelineRasterizationStateCreateInfo> RasterizationState;
	std::optional<vk::PipelineMultisampleStateCreateInfo> MultisampleState;
	std::optional<vk::PipelineDepthStencilStateCreateInfo> DepthStencilState;

	vk::PipelineColorBlendStateCreateInfo ColorBlendState;
	std::vector<vk::PipelineColorBlendAttachmentState> ColorBlendAttachments;

	vk::PipelineDynamicStateCreateInfo DynamicState;
	std::vector<vk::DynamicState> DynamicStates;

	vk::PipelineLayout layout;
	vk::RenderPass renderPass = {};
	uint32_t subpass = {};
	vk::Pipeline basePipelineHandle;
	int32_t basePipelineIndex = {};

	operator vk::GraphicsPipelineCreateInfo()
	{
		VertexInputState.setVertexBindingDescriptions(VertexBindingDescriptions);
		VertexInputState.setVertexAttributeDescriptions(VertexAttributeDescriptions);

		ViewportState.setViewports(Viewports);
		ViewportState.setScissors(Scissors);

		ColorBlendState.setAttachments(ColorBlendAttachments);

		DynamicState.setDynamicStates(DynamicStates);

		return GraphicsPipelineCreateInfo{
		        .flags = flags,
		        .stageCount = (uint32_t)Stages.size(),
		        .pStages = Stages.data(),
		        .pVertexInputState = &VertexInputState,
		        .pInputAssemblyState = InputAssemblyState ? &*InputAssemblyState : nullptr,
		        .pTessellationState = TessellationState ? &*TessellationState : nullptr,
		        .pViewportState = &ViewportState,
		        .pRasterizationState = RasterizationState ? &*RasterizationState : nullptr,
		        .pMultisampleState = MultisampleState ? &*MultisampleState : nullptr,
		        .pDepthStencilState = DepthStencilState ? &*DepthStencilState : nullptr,
		        .pColorBlendState = &ColorBlendState,
		        .pDynamicState = &DynamicState,
		        .layout = layout,
		        .renderPass = renderPass,
		        .subpass = subpass,
		        .basePipelineHandle = basePipelineHandle,
		        .basePipelineIndex = basePipelineIndex,
		};
	}
};
} // namespace vk
