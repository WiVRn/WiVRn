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

#pragma once

#include <span>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class growable_descriptor_pool
{
	friend struct descriptor_set;

	struct pool
	{
		int free_count;
		vk::raii::DescriptorPool descriptor_pool;
	};

	vk::raii::Device & device;
	vk::DescriptorSetLayout layout_;

	int descriptorsets_per_pool;
	std::vector<vk::DescriptorPoolSize> size;
	std::vector<pool> pools;

public:
	growable_descriptor_pool(vk::raii::Device & device, vk::raii::DescriptorSetLayout & layout, std::span<const vk::DescriptorSetLayoutBinding> bindings, int descriptorsets_per_pool = 100);

	std::shared_ptr<vk::raii::DescriptorSet> allocate();
};
