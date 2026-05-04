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
#include <string>
#include <string_view>
#include <utility>
#include <openxr/openxr.h>

enum class model
{
	oculus_quest,
	oculus_quest_2,
	meta_quest_pro,
	meta_quest_3,
	meta_quest_3s,
	pico_neo_3,
	pico_4,
	pico_4s,
	pico_4_pro,
	pico_4_enterprise,
	htc_vive_focus_3,
	htc_vive_xr_elite,
	htc_vive_focus_vision,
	lynx_r1,
	samsung_galaxy_xr,
	unknown
};

enum class feature
{
	microphone,
	hand_tracking,
	eye_gaze,
	face_tracking,
	body_tracking,
};

struct hmd_permissions
{
	const char * hand_tracking = nullptr;
	const char * eye_gaze = nullptr;
	const char * face_tracking = nullptr;
	const char * body_tracking = nullptr;
};

struct hmd_traits
{
	const char * controller_profile = "generic-trigger-squeeze";
	const char * controller_ray_model = "assets://ray.glb";
	XrVersion max_openxr_api_version = XR_API_VERSION_1_1;
	uint32_t panel_width_override = 0;
	bool needs_srgb_conversion = true;
	const hmd_permissions * permissions = nullptr;
};

model guess_model();
std::string model_name();
void initialize_runtime_hmd_traits();
// Initialized once at startup (initialize_runtime_hmd_traits()).
// If the HMD is recognized, returns traits specific to that model; otherwise,returns
// default behavior.
// Fallback: if called before initialize_runtime_hmd_traits() (avoid this!), dynamically
// constructs itself and complains in logs.
const hmd_traits & runtime_hmd_traits();
const char * permission_name_for_hmd(const hmd_traits & traits, const feature f);

XrViewConfigurationView override_view(XrViewConfigurationView, model = guess_model());

std::pair<glm::vec3, glm::quat> controller_offset(std::string_view profile, xr::spaces space);
