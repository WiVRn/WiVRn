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

#include <vector>
#include <vulkan/vulkan_raii.hpp>

class growable_descriptor_pool
{
public:
	struct sizes
	{
		int sampler = 0;
		int combined_image_sampler = 0;
		int sampled_image = 0;
		int storage_image = 0;
		int uniform_texel_buffer = 0;
		int storage_texel_buffer = 0;
		int uniform_buffer = 0;
		int storage_buffer = 0;
		int uniform_buffer_dynamic = 0;
		int storage_buffer_dynamic = 0;
		int input_attachment = 0;
	};

private:
	vk::raii::Device & device;
	std::vector<vk::DescriptorPoolSize> size;
	int descriptorsets_per_pool;

	std::vector<vk::raii::DescriptorPool> pools;

public:
	growable_descriptor_pool(vk::raii::Device & device, sizes size, int descriptorsets_per_pool);

	std::shared_ptr<vk::raii::DescriptorSet> allocate(vk::DescriptorSetLayout layout);

	std::shared_ptr<vk::raii::DescriptorSet> allocate(vk::raii::DescriptorSetLayout& layout)
	{
		return allocate(*layout);
	}
};
