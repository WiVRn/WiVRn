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

#include <openxr/openxr.h>

enum class model
{
	oculus_quest,
	oculus_quest_2,
	meta_quest_pro,
	meta_quest_3,
	pico_neo_3,
	pico_4,
	htc_vive_focus_3,
	htc_vive_xr_elite,
	unknown
};

model guess_model();

XrViewConfigurationView override_view(XrViewConfigurationView, model);

bool use_runtime_reprojection();
