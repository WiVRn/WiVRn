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

#include <filesystem>
#include <glm/glm.hpp>
#include <magic_enum_containers.hpp>
#include <render/scene_data.h>
#include <span>
#include <variant>
#include <xr/space.h>
#include <openxr/openxr.h>

struct input_profile
{
	struct node_state_transform
	{
		glm::vec3 position;
		glm::quat orientation;
	};

	struct node_state_visibility
	{
	};

	using node_state = std::variant<std::pair<node_state_transform, node_state_transform>, node_state_visibility>;

	struct node_target
	{
		node_handle node;
		node_state state;
	};

	struct visual_response
	{
		XrAction action;
		XrActionType type;
		int axis; // Only if type is XR_ACTION_TYPE_VECTOR2F_INPUT
		float bias;
		float scale;

		node_target target;
	};

	std::string id;

	std::vector<visual_response> responses;
	std::vector<std::pair<xr::spaces, node_handle>> model_handles;
	node_handle left_ray;
	node_handle right_ray;

	magic_enum::containers::array<xr::spaces, std::pair<glm::vec3, glm::quat>> offset;

	input_profile(const std::filesystem::path & json_profile, scene_loader & loader, scene_data & scene_controllers, scene_data & scene_rays);

	// application::poll_actions() must have been called before
	void apply(XrSpace world_space, XrTime predicted_display_time, bool hide_left, bool hide_right, std::span<glm::vec4> pointer_limits);
};
