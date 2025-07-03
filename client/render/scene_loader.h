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

#include <filesystem>
#include <vulkan/vulkan_raii.hpp>

#include "scene_components.h"

class scene_loader
{
	vk::raii::Device & device;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Queue & queue;
	uint32_t queue_family_index;
	std::shared_ptr<renderer::material> default_material;

public:
	scene_loader(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, vk::raii::Queue & queue, uint32_t queue_family_index, std::shared_ptr<renderer::material> default_material) :
	        device(device),
	        physical_device(physical_device),
	        queue(queue),
	        queue_family_index(queue_family_index),
	        default_material(default_material)
	{}

	entt::registry operator()(const std::filesystem::path & gltf_path);

	void add_prefab(entt::registry & scene, const entt::registry & prefab, entt::entity root = entt::null);
};
