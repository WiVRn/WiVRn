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

#include "xr/swapchain.h"
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <openxr/openxr.h>
#include <optional>
#include <utility>
#include <span>
#include <vector>

class imgui_context
{
	struct command_buffer
	{
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Fence fence = nullptr;
	};

public:
	struct imgui_frame
	{
		vk::Image destination;
		vk::raii::ImageView image_view_framebuffer = nullptr;
		vk::raii::Framebuffer framebuffer = nullptr;
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
	vk::raii::Device& device;
	uint32_t queue_family_index;
	vk::raii::Queue& queue;

	vk::raii::Pipeline pipeline = nullptr;
	vk::raii::DescriptorPool descriptor_pool;
	vk::raii::RenderPass renderpass;
	vk::raii::CommandPool command_pool;

	std::vector<imgui_frame> frames;
	imgui_frame& get_frame(vk::Image destination);

	std::vector<command_buffer> command_buffers;
	size_t current_command_buffer = 0;
	command_buffer& get_command_buffer()
	{
		return command_buffers[current_command_buffer];
	}

	vk::Extent2D size;
	vk::Format format;
	vk::ClearValue clear_value = {vk::ClearColorValue{0,0,0,0}};

	glm::vec3 position_ = {0, 1, -1.5};
	glm::quat orientation_ = {1, 0, 0, 0};
	glm::vec2 scale_;

	xr::swapchain& swapchain;

	ImGuiContext * context;
	ImGuiIO& io;

	std::vector<std::pair<controller, controller_state>> controllers;
	XrSpace world;
	size_t focused_controller = (size_t)-1;
	XrTime last_display_time = 0;

	bool button_pressed = false;

	std::optional<ImVec2> ray_plane_intersection(const imgui_context::controller_state& in);

public:
	imgui_context(vk::raii::Device& device, uint32_t queue_family_index, vk::raii::Queue& queue, XrSpace world, std::span<controller> controllers, xr::swapchain& swapchain, glm::vec2 size);
	~imgui_context();

	void set_position(glm::vec3 position, glm::quat orientation)
	{
		position_ = position;
		orientation_ = orientation;
	}

	XrCompositionLayerQuad composition_layer(XrSwapchain swapchain) const
	{
		return XrCompositionLayerQuad{
			.type = XR_TYPE_COMPOSITION_LAYER_QUAD,
			.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
			.space = world,
			.eyeVisibility = XrEyeVisibility::XR_EYE_VISIBILITY_BOTH,
			.subImage = {
				.swapchain = swapchain,
				.imageRect = {
					.offset = {0, 0},
					.extent = {(int32_t)size.width, (int32_t)size.height}},
			},
			.pose = pose(),
			.size = scale(),
		};
	}

	XrPosef pose() const
	{
		return XrPosef{
			.orientation = {
				.x = orientation_.x,
				.y = orientation_.y,
				.z = orientation_.z,
				.w = orientation_.w,
			},
			.position = {
				.x = position_.x,
				.y = position_.y,
				.z = position_.z,
			}
		};
	}

	glm::vec3& position()
	{
		return position_;
	}

	glm::quat& orientation()
	{
		return orientation_;
	}

	glm::vec3 position() const
	{
		return position_;
	}

	glm::quat orientation() const
	{
		return orientation_;
	}

	XrExtent2Df scale() const
	{
		return { scale_.x, scale_.y };
	}

	void new_frame(XrTime display_time);

	void render(vk::Image destination);

	ImFont * large_font;
	size_t get_focused_controller() const
	{
		return focused_controller;
	}
};
