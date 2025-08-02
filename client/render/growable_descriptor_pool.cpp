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

#include <algorithm>
#include <stdexcept>

struct descriptor_set
{
	growable_descriptor_pool * growable_pool;
	vk::DescriptorPool pool;
	vk::raii::DescriptorSet ds;

	descriptor_set(const descriptor_set &) = delete;
	descriptor_set & operator=(const descriptor_set &) = delete;

	descriptor_set(growable_descriptor_pool * growable_pool,
	               vk::DescriptorPool pool,
	               vk::raii::DescriptorSet ds) :
	        growable_pool(growable_pool),
	        pool(pool),
	        ds(std::move(ds))
	{
	}

	descriptor_set(descriptor_set && other) :
	        growable_pool(std::exchange(other.growable_pool, nullptr)),
	        pool(other.pool),
	        ds(std::move(other.ds))
	{
	}

	descriptor_set & operator=(descriptor_set && other)
	{
		std::swap(*this, other);
		return *this;
	}

	~descriptor_set()
	{
		if (not growable_pool)
			return;

		for (auto & i: growable_pool->pools)
		{
			if (*i.descriptor_pool == pool)
			{
				i.free_count++;
				return;
			}
		}
	}
};

growable_descriptor_pool::growable_descriptor_pool(
        vk::raii::Device & device,
        vk::raii::DescriptorSetLayout & layout,
        std::span<const vk::DescriptorSetLayoutBinding> bindings,
        int descriptorsets_per_pool) :
        device(device),
        layout_(nullptr),
        descriptorsets_per_pool(descriptorsets_per_pool)
{
	if (descriptorsets_per_pool <= 0)
		throw std::invalid_argument("descriptorsets_per_pool");

	for (vk::DescriptorSetLayoutBinding binding: bindings)
	{
		if (auto it = std::find_if(size.begin(), size.end(), [&binding](vk::DescriptorPoolSize & x) { return x.type == binding.descriptorType; }); it != size.end())
		{
			it->descriptorCount += binding.descriptorCount * descriptorsets_per_pool;
		}
		else if (binding.descriptorCount > 0)
		{
			size.push_back(vk::DescriptorPoolSize{
			        .type = binding.descriptorType,
			        .descriptorCount = binding.descriptorCount * descriptorsets_per_pool,
			});
		}
	}

	layout_ = *layout;
}

std::shared_ptr<vk::raii::DescriptorSet> growable_descriptor_pool::allocate()
{
	vk::DescriptorSetAllocateInfo alloc_info{
	        .descriptorSetCount = 1,
	        .pSetLayouts = &layout_,
	};

	for (pool & i: pools)
	{
		if (i.free_count)
		{
			i.free_count--;
			alloc_info.descriptorPool = *i.descriptor_pool;

			auto ds = std::make_shared<descriptor_set>(this, *i.descriptor_pool, std::move(device.allocateDescriptorSets(alloc_info)[0]));

			return std::shared_ptr<vk::raii::DescriptorSet>(ds, &ds->ds);
		}
	}

	vk::DescriptorPoolCreateInfo pool_info{
	        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
	        .maxSets = (uint32_t)descriptorsets_per_pool};

	pool_info.setPoolSizes(size);

	pools.push_back(pool{
	        .free_count = descriptorsets_per_pool - 1,
	        .descriptor_pool = vk::raii::DescriptorPool(device, pool_info),
	});

	alloc_info.descriptorPool = *pools.back().descriptor_pool;

	auto ds = std::make_shared<descriptor_set>(this, *pools.back().descriptor_pool, std::move(device.allocateDescriptorSets(alloc_info)[0]));

	return std::shared_ptr<vk::raii::DescriptorSet>(ds, &ds->ds);
}
