/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "configuration.h"
#include "xr/space.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum_containers.hpp>
#include <string>
#include <utility>
#include <openxr/openxr.h>

using hmd_permissions = magic_enum::containers::array<feature, const char *>;

class hmd_traits
{
public:
	std::string controller_profile = "generic-trigger-squeeze";
	std::string controller_ray_model = "assets://ray.glb";
	hmd_permissions permissions{};
	XrVersion max_openxr_api_version = XR_API_VERSION_1_1;
	uint32_t panel_width_override = 0;
	bool needs_srgb_conversion = true;
	bool view_locate = true; // can locate relative to view
	bool vk_debug_ext_allowed = true;
	bool bind_simple_controller = true;
	bool hand_interaction_grip_surface = true;
	bool pico_face_tracker = false;
	bool discard_frame = true; // can do xrBeginFrame twice to discard the first one
#ifndef NDEBUG
private:
	bool initialized_ = false;
#endif

public:
	hmd_traits();

	void init();

	const char * permission_name(feature f) const
	{
		return permissions[f];
	}

	std::string model_name() const;
	std::pair<glm::vec3, glm::quat> controller_offset(xr::spaces space) const;
	XrViewConfigurationView override_view(XrViewConfigurationView) const;

	bool is_initialized() const
	{
#ifndef NDEBUG
		return initialized_;
#else
		return true;
#endif
	}
};
