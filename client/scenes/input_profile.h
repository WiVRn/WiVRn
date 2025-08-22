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

#include "scene.h"
#include <cstdint>
#include <entt/fwd.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <magic_enum_containers.hpp>
#include <span>
#include <xr/space.h>
#include <openxr/openxr.h>

struct input_profile
{
	std::string id;

	entt::entity left_ray;
	entt::entity right_ray;

	magic_enum::containers::array<xr::spaces, std::pair<glm::vec3, glm::quat>> offset;

	input_profile(scene & scene, const std::filesystem::path & json_profile, uint32_t layer_mask_controller, uint32_t layer_mask_ray);

	// application::poll_actions() must have been called before
	void apply(entt::registry & scene, XrSpace world_space, XrTime predicted_display_time, bool hide_left, bool hide_right, std::span<glm::vec4> pointer_limits);
};
