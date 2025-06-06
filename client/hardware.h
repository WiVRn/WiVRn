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

model guess_model();
std::string model_name();

XrViewConfigurationView override_view(XrViewConfigurationView, model = guess_model());

bool need_srgb_conversion(model);

// Return nullptr if no permission is required
const char * permission_name(feature f);

std::string controller_name();
std::string controller_ray_model_name();
std::pair<glm::vec3, glm::quat> controller_offset(std::string_view profile, xr::spaces space);
