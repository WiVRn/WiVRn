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

#include <entt/core/fwd.hpp>
#include <filesystem>
#include <functional>
#include <vulkan/vulkan_raii.hpp>

#include "scene_components.h"
#include "utils/thread_safe.h"

class scene_loader
{
	vk::raii::Device & device;
	vk::raii::PhysicalDevice physical_device;
	thread_safe<vk::raii::Queue> & queue;
	uint32_t queue_family_index;
	std::shared_ptr<renderer::material> default_material;
	std::filesystem::path texture_cache;

	std::shared_ptr<entt::registry> operator()(
	        std::span<const std::byte> data,
	        const std::string & name = "",
	        const std::filesystem::path & parent_path = "",
	        const std::filesystem::path & gltf_texture_cache = "",
	        std::function<void(float)> progress_cb = {});

public:
	scene_loader(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice physical_device,
	        thread_safe<vk::raii::Queue> & queue,
	        uint32_t queue_family_index,
	        std::shared_ptr<renderer::material> default_material,
	        std::filesystem::path texture_cache);

	scene_loader(const scene_loader &) = default;

	std::shared_ptr<entt::registry> operator()(
	        const std::filesystem::path & gltf_path,
	        std::function<void(float)> progress_cb = {});

	void clear_texture_cache();
};
