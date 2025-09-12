/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
class instance;
class session;
class foveation_profile : public utils::handle<XrFoveationProfileFB>
{
	XrFoveationLevelFB level_;
	float vertical_offset_degrees_;
	bool dynamic_;

public:
	foveation_profile(instance & inst, session & s, XrFoveationLevelFB level, float vertical_offset_degrees, bool dynamic);

	bool operator==(const foveation_profile & other) const
	{
		return level_ == other.level_ and vertical_offset_degrees_ == other.vertical_offset_degrees_ and dynamic_ == other.dynamic_;
	}

	XrFoveationLevelFB level() const
	{
		return level_;
	}

	float vertical_offset_degrees() const
	{
		return vertical_offset_degrees_;
	}

	bool dynamic() const
	{
		return dynamic_;
	}
};
} // namespace xr
