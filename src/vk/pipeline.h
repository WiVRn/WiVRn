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

#include "pipeline_layout.h"
#include "utils/handle.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace vk
{
class pipeline : public utils::handle<VkPipeline>
{
	VkDevice device{};

public:
	struct graphics_info
	{
		std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
		std::vector<VkVertexInputBindingDescription> vertex_input_bindings;
		std::vector<VkVertexInputAttributeDescription> vertex_input_attributes;
		VkPipelineInputAssemblyStateCreateInfo InputAssemblyState{};
		// VkPipelineTessellationStateCreateInfo TessellationState;
		std::vector<VkViewport> viewports;
		std::vector<VkRect2D> scissors;
		VkPipelineRasterizationStateCreateInfo RasterizationState{};
		VkPipelineMultisampleStateCreateInfo MultisampleState{};
		VkPipelineDepthStencilStateCreateInfo DepthStencilState{};
		VkPipelineColorBlendStateCreateInfo ColorBlendState{};
		std::vector<VkDynamicState> dynamic_states;
		VkRenderPass renderPass{};
		uint32_t subpass{};
	};

	pipeline() = default;
	pipeline(VkDevice device, graphics_info & create_info, VkPipelineLayout layout);
	pipeline(const pipeline &) = delete;
	pipeline(pipeline &&) = default;
	pipeline & operator=(const pipeline &) = delete;
	pipeline & operator=(pipeline &&) = default;
	~pipeline();
};
} // namespace vk
