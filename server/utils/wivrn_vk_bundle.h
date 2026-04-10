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
#include <mutex>
#include <type_traits>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

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

struct vk_bundle
{
	vk::raii::Context vk_ctx;
	vk::raii::Instance instance;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device device;
	std::optional<vk_allocator> allocator;

	bool busy_wait;

	std::mutex queue_mutex;
	vk::raii::Queue queue;
	uint32_t queue_family_index;

	std::mutex transfer_queue_mutex;
	vk::raii::Queue transfer_queue;
	uint32_t transfer_queue_family_index;

	std::mutex encode_queue_mutex;
	vk::raii::Queue encode_queue;
	uint32_t encode_queue_family_index;

	vk::raii::DebugUtilsMessengerEXT debug;

	vk::StructureChain<
	        vk::PhysicalDeviceFeatures2,
#ifdef VK_KHR_video_maintenance1
	        vk::PhysicalDeviceVideoMaintenance1FeaturesKHR,
#endif
#ifdef VK_KHR_maintenance9
	        vk::PhysicalDeviceMaintenance9FeaturesKHR,
#endif
#ifdef VK_KHR_video_encode_intra_refresh
	        vk::PhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR,
#endif
	        vk::PhysicalDeviceVulkan12Features,
	        vk::PhysicalDeviceVulkan13Features>
	        feat{};

	std::vector<const char *> instance_extensions;
	std::vector<const char *> device_extensions;

	vk_bundle();

	uint32_t get_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags memory_props);

	template <typename T, typename U>
	void name(const T & handle, U && value)
	{
		return name(T::objectType, vk_handle(handle), std::forward<U>(value));
	}

	bool has_instance_ext(const char *) const;
	bool has_device_ext(const char *) const;

	// return true if optimal images do NOT require a transfer operation
	// between those queues
	bool optimal_transfer(uint32_t from_queue_family_index, uint32_t to_queue_family_index) const;

	// Wrappers that do busy waiting on NVIDIA
	vk::Result waitForFence(vk::raii::Fence &, uint64_t timeout_ns);
	vk::Result waitSemaphore(vk::raii::Semaphore &, uint64_t value, uint64_t timeout_ns);

	vk::raii::ShaderModule load_shader(const char * name);

private:
	void name(vk::ObjectType, uint64_t handle, const char * value);
	void name(vk::ObjectType t, uint64_t handle, const std::string & value)
	{
		return name(t, handle, value.c_str());
	}
};
} // namespace wivrn
