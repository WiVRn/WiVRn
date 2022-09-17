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

#include "utils/handle.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace vk
{
class pipeline_layout : public utils::handle<VkPipelineLayout>
{
	VkDevice device{};

public:
	struct info
	{
		std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
		std::vector<VkPushConstantRange> push_constant_ranges;
	};

	~pipeline_layout();
	pipeline_layout() = default;
	pipeline_layout(VkDevice device, const info & create_info);
	pipeline_layout(const pipeline_layout &) = delete;
	pipeline_layout(pipeline_layout &&) = default;
	pipeline_layout & operator=(const pipeline_layout &) = delete;
	pipeline_layout & operator=(pipeline_layout &&) = default;
};
} // namespace vk
