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

#include "render/scene_data.h"
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <openxr/openxr.h>
#include "vk/allocation.h"

class imgui_context
{
public:
	struct imgui_frame
	{
		image_allocation image;
		vk::raii::ImageView image_view_framebuffer = nullptr;
		vk::raii::ImageView image_view_texture = nullptr;
		vk::raii::Framebuffer framebuffer = nullptr;
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Fence fence = nullptr;
	};

	struct imgui_viewport
	{
		static constexpr int frames_in_flight = 2;
		uint32_t num_mipmaps;

		std::array<imgui_frame, frames_in_flight> frames;
		int frameindex = 0;

		vk::raii::Device& device;

		vk::Extent2D size;
		vk::ClearValue clear_value = {vk::ClearColorValue{0,0,0,0}};

		// TODO: sync with 3d scene
		glm::vec3 position = {0, 1, -1.5};
		glm::quat orientation = {1, 0, 0, 0};
		glm::vec2 scale = {1, 1};

		imgui_viewport(vk::raii::Device& device, vk::raii::CommandPool& command_pool, vk::RenderPass renderpass, vk::Extent2D size, vk::Format format);
	};

	struct controller
	{
		XrSpace aim;
		XrAction trigger; // XR_ACTION_TYPE_FLOAT_INPUT
		XrAction squeeze; // XR_ACTION_TYPE_FLOAT_INPUT
		XrAction scroll;  // XR_ACTION_TYPE_VECTOR2F_INPUT

		// TODO: thresholds?
	};

	struct controller_state
	{
		bool active;

		glm::vec3 aim_position;
		glm::quat aim_orientation;

		float trigger_value;
		float squeeze_value;
		glm::vec2 scroll_value;

		bool squeeze_clicked;
		bool trigger_clicked;
	};

private:
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

	// shared_ptr because it needs to be kept alive until the output textures are not used anymore
	std::shared_ptr<imgui_viewport> viewport;

	ImGuiContext * context;
	ImGuiIO& io;

	// std::string ini_filename;

	std::vector<std::pair<controller, controller_state>> controllers;
	XrSpace world;
	size_t focused_controller = (size_t)-1;
	XrTime last_display_time = 0;

	bool button_pressed = false;

public:
	imgui_context(vk::raii::Device& device, uint32_t queue_family_index,
	vk::raii::Queue& queue, XrSpace world, std::span<controller> controllers, float resolution, glm::vec2 scale);
	~imgui_context();

	void set_position(glm::vec3 position, glm::quat orientation);
	void new_frame(XrTime display_time);
	std::shared_ptr<vk::raii::ImageView> render();

	ImFont * large_font;
	size_t get_focused_controller() const
	{
		return focused_controller;
	}
};
