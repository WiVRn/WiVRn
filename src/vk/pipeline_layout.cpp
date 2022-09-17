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

#include "pipeline_layout.h"
#include "utils/check.h"

vk::pipeline_layout::pipeline_layout(VkDevice device, const info & create_info) :
        device(device)
{
	VkPipelineLayoutCreateInfo plci{
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	        .setLayoutCount = (uint32_t)create_info.descriptor_set_layouts.size(),
	        .pSetLayouts = create_info.descriptor_set_layouts.data(),
	        .pushConstantRangeCount = (uint32_t)create_info.push_constant_ranges.size(),
	        .pPushConstantRanges = create_info.push_constant_ranges.data()};
	CHECK_VK(vkCreatePipelineLayout(device, &plci, nullptr, &id));
}

vk::pipeline_layout::~pipeline_layout()
{
	if (device)
		vkDestroyPipelineLayout(device, id, nullptr);
}
