/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "vk_allocator.h"
#include "vk_mem_alloc.h"
#include <cstdint>
#include <vulkan/vulkan_raii.hpp>

template <typename T>
struct basic_allocation_traits
{
};

struct basic_allocation_traits_base
{
	static void * map(VmaAllocation allocation);
	static void unmap(VmaAllocation allocation);
};

template <>
struct basic_allocation_traits<vk::Buffer> : basic_allocation_traits_base
{
	using CreateInfo = vk::BufferCreateInfo;
	using NativeCreateInfo = CreateInfo::NativeType;
	using RaiiType = vk::raii::Buffer;

	static std::pair<RaiiType, VmaAllocation> create(
	        vk::raii::Device & device,
	        const CreateInfo & buffer_info,
	        const VmaAllocationCreateInfo & alloc_info);

	static void destroy(
	        RaiiType & buffer,
	        VmaAllocation allocation,
	        void * mapped);
};

template <>
struct basic_allocation_traits<vk::Image> : basic_allocation_traits_base
{
	using CreateInfo = vk::ImageCreateInfo;
	using NativeCreateInfo = CreateInfo::NativeType;
	using RaiiType = vk::raii::Image;

	static std::pair<RaiiType, VmaAllocation> create(
	        vk::raii::Device & device,
	        const CreateInfo & image_info,
	        const VmaAllocationCreateInfo & alloc_info);

	static void destroy(
	        RaiiType & image,
	        VmaAllocation allocation,
	        void * mapped);
};

template <typename T>
class basic_allocation
{
public:
	using CType = T::CType;
	using traits = basic_allocation_traits<T>;
	using CreateInfo = traits::CreateInfo;
	using NativeCreateInfo = CreateInfo::NativeType;
	using RaiiType = traits::RaiiType;

private:
	VmaAllocation allocation = nullptr;
	RaiiType resource = nullptr;
	void * mapped = nullptr;
	CreateInfo create_info{};

public:
	operator T()
	{
		return *resource;
	}

	operator const T() const
	{
		return *resource;
	}

	operator CType()
	{
		return CType(*resource);
	}

	operator const CType() const
	{
		return CType(*resource);
	}

	operator bool() const
	{
		return *resource;
	}

	operator VmaAllocation() const
	{
		return allocation;
	}

	RaiiType * operator->()
	{
		return &resource;
	}

	const T * operator->() const
	{
		return &resource;
	}

	basic_allocation() = default;
	basic_allocation(vk::raii::Device & device, const CreateInfo & create_info, const VmaAllocationCreateInfo & alloc_info) :
	        create_info(create_info)
	{
		std::tie(resource, allocation) = traits::create(device, create_info, alloc_info);
	}

	basic_allocation(vk::raii::Device & device, const CreateInfo & create_info, const VmaAllocationCreateInfo & alloc_info, const std::string & name) :
	        create_info(create_info)
	{
		std::tie(resource, allocation) = traits::create(device, create_info, alloc_info);

		vmaSetAllocationName(vk_allocator::instance(), allocation, name.c_str());
		if (vk_allocator::instance().has_debug_utils)
			device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
			        .objectType = T::objectType,
			        .objectHandle = uint64_t(CType(*resource)),
			        .pObjectName = name.c_str(),
			});
	}

	basic_allocation(VmaAllocation allocation, RaiiType && resource) :
	        allocation(allocation), resource(std::move(resource))
	{}

	basic_allocation(VmaAllocation allocation, vk::raii::Device & device, RaiiType::CppType resource) :
	        allocation(allocation), resource(RaiiType{device, resource})
	{}

	basic_allocation(VmaAllocation allocation, vk::raii::Device & device, RaiiType::CType resource) :
	        allocation(allocation), resource(RaiiType{device, resource})
	{}

	basic_allocation(VmaAllocation allocation, vk::raii::Device & device, RaiiType && resource, const std::string & name) :
	        allocation(allocation), resource(std::move(resource))
	{
		vmaSetAllocationName(vk_allocator::instance(), allocation, name.c_str());
		if (vk_allocator::instance().has_debug_utils)
			device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
			        .objectType = T::objectType,
			        .objectHandle = uint64_t(CType(*resource)),
			        .pObjectName = name.c_str(),
			});
	}

	basic_allocation(VmaAllocation allocation, vk::raii::Device & device, RaiiType::CppType resource, const std::string & name) :
	        allocation(allocation), resource(RaiiType{device, resource})
	{
		vmaSetAllocationName(vk_allocator::instance(), allocation, name.c_str());
		if (vk_allocator::instance().has_debug_utils)
			device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
			        .objectType = T::objectType,
			        .objectHandle = uint64_t(CType(*resource)),
			        .pObjectName = name.c_str(),
			});
	}

	basic_allocation(VmaAllocation allocation, vk::raii::Device & device, RaiiType::CType resource, const std::string & name) :
	        allocation(allocation), resource(RaiiType{device, resource})
	{
		vmaSetAllocationName(vk_allocator::instance(), allocation, name.c_str());
		if (vk_allocator::instance().has_debug_utils)
			device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{
			        .objectType = T::objectType,
			        .objectHandle = uint64_t(resource),
			        .pObjectName = name.c_str(),
			});
	}

	basic_allocation(const basic_allocation &) = delete;
	basic_allocation(basic_allocation && other) :
	        allocation(other.allocation),
	        resource(std::move(other.resource)),
	        mapped(other.mapped),
	        create_info(other.create_info)
	{
		other.allocation = nullptr;
		other.mapped = nullptr;
	}

	const basic_allocation & operator=(const basic_allocation &) = delete;
	const basic_allocation & operator=(basic_allocation && other)
	{
		std::swap(allocation, other.allocation);
		std::swap(resource, other.resource);
		std::swap(mapped, other.mapped);
		std::swap(create_info, other.create_info);

		return *this;
	}

	~basic_allocation()
	{
		traits::destroy(resource, allocation, mapped);
	}

	void * map()
	{
		if (mapped)
			return mapped;

		mapped = traits::map(allocation);
		return mapped;
	}

	void unmap()
	{
		if (!mapped)
			return;

		traits::unmap(allocation);
		mapped = nullptr;
	}

	vk::DeviceSize size() const
	{
		VmaAllocationInfo info{};
		vmaGetAllocationInfo(vk_allocator::instance(), allocation, &info);
		return info.size;
	}

	template <typename U = uint8_t>
	U * data()
	{
		return reinterpret_cast<U *>(map());
	}

	const CreateInfo & info() const
	{
		return create_info;
	}

	auto properties() const
	{
		VkMemoryPropertyFlags result;
		vmaGetAllocationMemoryProperties(vk_allocator::instance(), allocation, &result);
		return result;
	}
};

using buffer_allocation = basic_allocation<vk::Buffer>;
using image_allocation = basic_allocation<vk::Image>;
