/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "vk/vk_allocator.h"
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

struct vk_bundle;

namespace wivrn
{

// to use vk::raii we need a vk::raii::{Instance,PhysicalDevice,Device}
// however we don't own it, so deactivate the destructor
struct raii_instance : public vk::raii::Instance
{
	raii_instance(vk::raii::Context & ctx, VkInstance i) :
	        vk::raii::Instance(ctx, i) {}
	raii_instance & operator=(raii_instance &&) noexcept = default;
	~raii_instance()
	{
		release();
	}
};
struct raii_device : public vk::raii::Device
{
	raii_device(vk::raii::PhysicalDevice & p, VkDevice d) :
	        vk::raii::Device(p, d) {}
	raii_device & operator=(raii_device &&) noexcept = default;
	~raii_device()
	{
		release();
	}
};

namespace details
{
template <typename T, typename U = void>
struct vk_handle
{
	uint64_t operator()(const T & handle)
	{
		typename T::CType chandle = handle;
		return uint64_t(chandle);
	}
};

// vk::raii specialization
template <typename T>
struct vk_handle<T, std::void_t<typename T::CppType>>
{
	uint64_t operator()(const T & handle)
	{
		typename T::CType chandle = *handle;
		return uint64_t(chandle);
	}
};
} // namespace details

template <typename T>
uint64_t vk_handle(const T & handle)
{
	return details::vk_handle<T>{}(handle);
}

struct wivrn_vk_bundle
{
	vk_bundle & vk;
	vk::raii::Context vk_ctx;
	raii_instance instance;
	vk::raii::PhysicalDevice physical_device;
	raii_device device;
	vk_allocator allocator;
	vk::raii::Queue queue;
	uint32_t queue_family_index;

	vk::raii::Queue encode_queue;
	uint32_t encode_queue_family_index;

	vk::raii::DebugUtilsMessengerEXT debug;

	std::vector<const char *> instance_extensions;
	std::vector<const char *> device_extensions;

	wivrn_vk_bundle(vk_bundle & vk, std::span<const char *> requested_instance_extensions, std::span<const char *> requested_device_extensions);

	uint32_t get_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags memory_props);

	template <typename T>
	void name(const T & handle, const char * value)
	{
		return name(T::objectType, vk_handle(handle), value);
	}

private:
	void name(vk::ObjectType, uint64_t handle, const char * value);
};
} // namespace wivrn
