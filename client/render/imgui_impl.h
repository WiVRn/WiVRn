/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "vk/allocation.h"

struct imgui_frame
{
	image_allocation image;
	vk::raii::ImageView image_view = nullptr;
	vk::raii::Framebuffer framebuffer = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;
	vk::raii::Fence fence = nullptr;
};

struct imgui_viewport
{
	static constexpr int frames_in_flight = 2;
	std::array<imgui_frame, frames_in_flight> frames;
	int frameindex = 0;

	vk::raii::Device& device;

	vk::Extent2D size;
	vk::ClearValue clear_value = {vk::ClearColorValue{0,0,0,0}};

	// TODO: sync with 3d scene
	glm::vec3 position = {0, 1, -1.5};
	glm::quat orientation = {1, 0, 0, 0};
	float scale = 1;

	imgui_viewport(vk::raii::Device& device, vk::raii::CommandPool& command_pool, vk::RenderPass renderpass, vk::Extent2D size, vk::Format format);
};

struct imgui_inputs
{
	bool active = false;
	int id;

	glm::vec3 controller_position;
	glm::quat controller_orientation;
	float squeeze;
	float trigger;
	glm::vec2 scroll;
};

struct imgui_controller_state
{
	bool active = false;
	int id;

	float squeeze;
	float trigger;

	bool squeeze_hysteresis;
	bool trigger_hysteresis;
};

class imgui_context
{
	static inline const std::array pool_sizes =
	{
		vk::DescriptorPoolSize{
			.type = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = 1,
		}
	};

	vk::raii::Device& device;
	uint32_t queue_family_index;
	vk::raii::Queue& queue;

	vk::raii::Pipeline pipeline = nullptr;
	vk::raii::DescriptorPool descriptor_pool;
	vk::raii::RenderPass renderpass;
	vk::raii::CommandPool command_pool;

	std::shared_ptr<imgui_viewport> viewport;

	ImGuiContext * context;
	ImGuiIO& io;

public:
	imgui_context(vk::raii::Device& device, uint32_t queue_family_index,
	vk::raii::Queue& queue);
	~imgui_context();

	void new_frame(std::span<imgui_inputs> inputs);
	std::shared_ptr<vk::raii::ImageView> render();
};
