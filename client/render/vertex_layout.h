/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "utils/magic_hash.h"
#include <cstddef>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>

namespace renderer
{
struct vertex_layout
{
	std::vector<vk::VertexInputBindingDescription> bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;
	std::vector<std::string> attribute_names;

	void add_attribute(std::string name, vk::Format format, uint32_t binding, uint32_t location, vk::VertexInputRate input_rate, int array_size = 1)
	{
		size_t texel_size = vk::blockSize(format);

		uint32_t offset = 0;

		if (auto iter = std::ranges::find(bindings, binding, &vk::VertexInputBindingDescription::binding); iter == bindings.end())
			bindings.push_back(vk::VertexInputBindingDescription{
			        .binding = binding,
			        .stride = (uint32_t)(texel_size * array_size),
			        .inputRate = input_rate,
			});
		else
		{
			offset = iter->stride;
			iter->stride += texel_size * array_size;
		}

		for (int index = 0; index < array_size; index++)
		{
			attributes.push_back(vk::VertexInputAttributeDescription{
			        .location = location + index,
			        .binding = binding,
			        .format = format,
			        .offset = (uint32_t)(offset + index * texel_size),
			});

			if (array_size == 1)
				attribute_names.push_back(name);
			else
				attribute_names.push_back(name + "_" + std::to_string(index));
		}
	}

	void add_vertex_attribute(std::string name, vk::Format format, uint32_t binding, uint32_t location, int array_size = 1)
	{
		add_attribute(std::move(name), format, binding, location, vk::VertexInputRate::eVertex, array_size);
	}

	bool operator==(const vertex_layout & other) const noexcept = default;
};
} // namespace renderer

namespace std
{
template <>
struct hash<renderer::vertex_layout> : utils::magic_hash<renderer::vertex_layout>
{};

template <>
struct hash<vk::VertexInputBindingDescription> : utils::magic_hash<vk::VertexInputBindingDescription>
{};

template <>
struct hash<vk::VertexInputAttributeDescription> : utils::magic_hash<vk::VertexInputAttributeDescription>
{};
} // namespace std
