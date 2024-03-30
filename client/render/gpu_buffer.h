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

#include "application.h"
#include "utils/alignment.h"
#include "vk/allocation.h"
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

class gpu_buffer
{
	std::vector<std::byte> bytes;
	vk::PhysicalDeviceLimits limits;
	fastgltf::Asset & asset;
	vk::BufferUsageFlags usage;

	size_t add(size_t alignment, const void * data_to_add, size_t size)
	{
		size_t offset = utils::align_up(alignment, bytes.size());

		bytes.resize(offset + size);

		memcpy(&bytes[offset], data_to_add, size);

		return offset;
	}

	template <typename T>
	        requires requires(T t) {
		        {
			        t.data()
		        } -> std::convertible_to<const void *>;
		        {
			        t.size()
		        } -> std::convertible_to<size_t>;
	        }
	size_t add(size_t alignment, const T & data_to_add)
	{
		return add(alignment, data_to_add.data(), data_to_add.size() * sizeof(data_to_add.data()[0]));
	}

public:
	gpu_buffer(vk::PhysicalDeviceProperties properties, fastgltf::Asset & asset) :
	        limits(properties.limits), asset(asset), usage(0) {}

	template <typename T>
	size_t add_uniform(const T & data_to_add)
	{
		usage |= vk::BufferUsageFlagBits::eUniformBuffer;

		size_t alignment = std::max(limits.minUniformBufferOffsetAlignment, alignof(T));
		return add(alignment, &data_to_add, sizeof(data_to_add));
	}

	template <typename T>
	size_t add_vertices(const T & data_to_add)
	{
		usage |= vk::BufferUsageFlagBits::eVertexBuffer;

		size_t alignment = std::max<size_t>(4, alignof(T));
		return add(alignment, data_to_add);
	}

	size_t add_indices(const fastgltf::Accessor & accessor)
	{
		usage |= vk::BufferUsageFlagBits::eIndexBuffer;

		switch (accessor.componentType)
		{
			case fastgltf::ComponentType::Byte:
			case fastgltf::ComponentType::UnsignedByte: {
				std::vector<uint8_t> indices(accessor.count);
				using index_t = std::remove_cvref_t<decltype(*indices.data())>;
				fastgltf::copyFromAccessor<index_t>(asset, accessor, indices.data());
				return add(4, indices);
			}

			case fastgltf::ComponentType::Short:
			case fastgltf::ComponentType::UnsignedShort: {
				std::vector<uint16_t> indices(accessor.count);
				using index_t = std::remove_cvref_t<decltype(*indices.data())>;
				fastgltf::copyFromAccessor<index_t>(asset, accessor, indices.data());
				return add(4, indices);
			}

			case fastgltf::ComponentType::Int:
			case fastgltf::ComponentType::UnsignedInt: {
				std::vector<uint32_t> indices(accessor.count);
				using index_t = std::remove_cvref_t<decltype(*indices.data())>;
				fastgltf::copyFromAccessor<index_t>(asset, accessor, indices.data());
				return add(4, indices);
			}

			default:
				throw std::runtime_error("Invalid index type");
		}
	}

	buffer_allocation copy_to_gpu()
	{
		buffer_allocation gpu_buffer{
		        application::get_device(),
		        vk::BufferCreateInfo{
		                .size = bytes.size(),
		                .usage = usage},
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO},
		        "gpu_buffer::copy_to_gpu"};

		memcpy(gpu_buffer.map(), bytes.data(), bytes.size());
		gpu_buffer.unmap();

		return gpu_buffer;
	}

	size_t size() const
	{
		return bytes.size();
	}
};
