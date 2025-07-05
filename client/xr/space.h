/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "utils/handle.h"
#include <openxr/openxr.h>

namespace xr
{
using space = utils::handle<XrSpace, xrDestroySpace>;

enum class spaces
{
	world,
	view,
	eye_gaze,
	grip_left,
	grip_right,
	aim_left,
	aim_right,
	palm_left,
	palm_right,
	// hand_interaction_ext
	pinch_left,
	pinch_right,
	poke_left,
	poke_right,

	count
};
} // namespace xr
