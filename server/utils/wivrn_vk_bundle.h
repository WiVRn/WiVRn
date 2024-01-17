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
#include <vector>
#include <vulkan/vulkan_raii.hpp>

struct vk_bundle;

// to use vk::raii we need a vk::raii::{Instance,PhysicalDevice,Device}
// however we don't own it, so deactivate the destructor
struct raii_instance : public vk::raii::Instance
{
	raii_instance(vk::raii::Context &ctx, VkInstance i) :
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

struct wivrn_vk_bundle
{
	vk::raii::Context vk_ctx;
	raii_instance instance;
	vk::raii::PhysicalDevice physical_device;
	raii_device device;
	vk_allocator allocator;
	vk::raii::Queue queue;
	uint32_t queue_family_index;

	std::vector<const char*> instance_extensions;
	std::vector<const char*> device_extensions;

	wivrn_vk_bundle(vk_bundle& vk, std::span<const char*> requested_instance_extensions, std::span<const char*> requested_device_extensions);

	uint32_t get_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags memory_props);
};
