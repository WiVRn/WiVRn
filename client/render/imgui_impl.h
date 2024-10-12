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

#include "xr/hand_tracker.h"
#include "xr/swapchain.h"
#include "wivrn_config.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <implot.h>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

class imgui_context
{
	struct command_buffer
	{
		vk::raii::CommandBuffer command_buffer = nullptr;
		vk::raii::Fence fence = nullptr;
	};

	struct texture_data
	{
		vk::raii::Sampler sampler;
		std::shared_ptr<vk::raii::ImageView> image_view;
		vk::raii::DescriptorSet descriptor_set;
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

		xr::hand_tracker * hand = nullptr;
	};

	struct controller_state
	{
		bool active = false;

		glm::vec3 aim_position = {0, 0, 0};
		glm::quat aim_orientation = {1, 0, 0, 0};

		float trigger_value = 0;
		float squeeze_value = 0;
		glm::vec2 scroll_value = {0, 0};

		float hover_distance = 1e10;

		bool squeeze_clicked = false;
		bool trigger_clicked = false;
		bool fingertip_hovered = false;
		bool fingertip_touched = false;
	};

private:
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	uint32_t queue_family_index;
	vk::raii::Queue & queue;

	vk::raii::Pipeline pipeline = nullptr;
	vk::raii::DescriptorPool descriptor_pool;
	vk::raii::DescriptorSetLayout ds_layout;
	vk::raii::RenderPass renderpass;
	vk::raii::CommandPool command_pool;

	std::unordered_map<ImTextureID, texture_data> textures;

	std::vector<imgui_frame> frames;
	imgui_frame & get_frame(vk::Image destination);

	std::vector<command_buffer> command_buffers;
	size_t current_command_buffer = 0;
	command_buffer & get_command_buffer()
	{
		return command_buffers[current_command_buffer];
	}

	vk::Extent2D size;
	vk::Format format;
	vk::ClearValue clear_value = {vk::ClearColorValue{0, 0, 0, 0}};

	glm::vec3 position_ = {0, 1, -1.5};
	glm::quat orientation_ = {1, 0, 0, 0};
	glm::vec2 scale_;

	xr::swapchain & swapchain;
	int image_index;

	ImGuiContext * context;
	ImPlotContext * plot_context;
	ImGuiIO & io;

	std::vector<std::pair<controller, controller_state>> controllers;
	XrSpace world;
	size_t focused_controller = (size_t)-1;
	XrTime last_display_time = 0;

	bool button_pressed = false;

	ImFontGlyphRangesBuilder glyph_range_builder;
	ImVector<ImWchar> glyph_ranges;
	bool glyph_range_dirty = true;

#if WIVRN_SHOW_IMGUI_DEMO_WINDOW
	bool show_demo_window = true;
#endif

	void initialize_fonts();

public:
	imgui_context(
	        vk::raii::PhysicalDevice physical_device,
	        vk::raii::Device & device,
	        uint32_t queue_family_index,
	        vk::raii::Queue & queue,
	        XrSpace world,
	        std::span<controller> controllers,
	        xr::swapchain & swapchain,
	        glm::vec2 size);

	~imgui_context();

	void set_position(glm::vec3 position, glm::quat orientation)
	{
		position_ = position;
		orientation_ = orientation;
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
		        },
		};
	}

	glm::vec3 & position()
	{
		return position_;
	}

	glm::quat & orientation()
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
		return {
		        scale_.x,
		        scale_.y,
		};
	}

	void new_frame(XrTime display_time);
	XrCompositionLayerQuad end_frame();

	ImFont * large_font;
	size_t get_focused_controller() const
	{
		return focused_controller;
	}

	std::optional<std::pair<ImVec2, float>> ray_plane_intersection(const imgui_context::controller_state & in) const;

	ImTextureID load_texture(const std::string & filename, vk::raii::Sampler && sampler);
	ImTextureID load_texture(const std::string & filename);
	void free_texture(ImTextureID);
	void set_current();

	void add_chars(std::string_view sv);
};
