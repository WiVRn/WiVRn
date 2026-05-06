/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "xr/space.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum_containers.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <openxr/openxr.h>

enum class feature
{
	microphone,
	hand_tracking,
	eye_gaze,
	face_tracking,
	body_tracking,
};

using hmd_permissions = magic_enum::containers::array<feature, const char *>;

extern const struct hmd_traits_t
{
	std::string controller_profile = "generic-trigger-squeeze";
	const char * controller_ray_model = "assets://ray.glb";
	hmd_permissions permissions{};
	XrVersion max_openxr_api_version = XR_API_VERSION_1_1;
	uint32_t panel_width_override = 0;
	bool needs_srgb_conversion = true;
	bool view_locate = true; // can locate relative to view
	bool vk_debug_ext_allowed = true;
	bool bind_simple_controller = true;
	bool hand_interaction_grip_surface = true;
	bool pico_face_tracker = false;

	XrViewConfigurationView override_view_for_hmd(XrViewConfigurationView) const;
} hmd_traits;

std::string model_name();

std::pair<glm::vec3, glm::quat> controller_offset(std::string_view profile, xr::spaces space);
