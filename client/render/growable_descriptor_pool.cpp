/*
 * WiVRn VR streaming
 * Copyright (C) 2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "growable_descriptor_pool.h"

#include <stdexcept>

growable_descriptor_pool::growable_descriptor_pool(vk::raii::Device & device, sizes size_, int descriptorsets_per_pool) :
        device(device), descriptorsets_per_pool(descriptorsets_per_pool)
{
	std::vector<vk::DescriptorPoolSize> tmp;

	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampler, .descriptorCount = (uint32_t)size_.sampler});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = (uint32_t)size_.combined_image_sampler});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = (uint32_t)size_.sampled_image});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = (uint32_t)size_.storage_image});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformTexelBuffer, .descriptorCount = (uint32_t)size_.uniform_texel_buffer});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageTexelBuffer, .descriptorCount = (uint32_t)size_.storage_texel_buffer});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = (uint32_t)size_.uniform_buffer});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = (uint32_t)size_.storage_buffer});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBufferDynamic, .descriptorCount = (uint32_t)size_.uniform_buffer_dynamic});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBufferDynamic, .descriptorCount = (uint32_t)size_.storage_buffer_dynamic});
	tmp.push_back(vk::DescriptorPoolSize{.type = vk::DescriptorType::eInputAttachment, .descriptorCount = (uint32_t)size_.input_attachment});

	for (auto & i: tmp)
	{
		// Only use the descriptor types that have a descriptorCount above 0
		if (i.descriptorCount > 0)
			size.push_back(i);
	}

	if (size.empty())
		throw std::invalid_argument("size");

	if (descriptorsets_per_pool <= 0)
		throw std::invalid_argument("descriptorsets_per_pool");
}

std::shared_ptr<vk::raii::DescriptorSet> growable_descriptor_pool::allocate(vk::DescriptorSetLayout layout)
{
	vk::DescriptorSetAllocateInfo alloc_info{
	        .descriptorSetCount = 1,
	        .pSetLayouts = &layout,
	};

	for (vk::raii::DescriptorPool & i: pools)
	{
		// TODO: don't use exceptions as control flow
		try
		{
			alloc_info.descriptorPool = *i;
			return std::make_shared<vk::raii::DescriptorSet>(std::move(device.allocateDescriptorSets(alloc_info)[0]));
		}
		catch (vk::OutOfPoolMemoryError &)
		{
			continue;
		}
	}

	vk::DescriptorPoolCreateInfo pool_info{
	        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
	        .maxSets = (uint32_t)descriptorsets_per_pool};

	pool_info.setPoolSizes(size);

	alloc_info.descriptorPool = *pools.emplace_back(device, pool_info);

	return std::make_shared<vk::raii::DescriptorSet>(std::move(device.allocateDescriptorSets(alloc_info)[0]));
}
