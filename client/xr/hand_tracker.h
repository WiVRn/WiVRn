/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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
#include <array>
#include <utility>

#include "instance.h"

namespace xr
{
class hand_tracker : public utils::handle<XrHandTrackerEXT>
{
	PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT{};
	PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT{};

public:
	hand_tracker() = default;
	hand_tracker(instance & inst, XrHandTrackerEXT h)
	{
		id = h;
		xrLocateHandJointsEXT = inst.get_proc<PFN_xrLocateHandJointsEXT>("xrLocateHandJointsEXT");
		xrDestroyHandTrackerEXT = inst.get_proc<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT");
	}
	hand_tracker(hand_tracker &&) = default;
	hand_tracker & operator=(hand_tracker &&) = default;

	~hand_tracker()
	{
		if (id != XR_NULL_HANDLE && xrDestroyHandTrackerEXT)
			xrDestroyHandTrackerEXT(id);
	}

	using joint = std::pair<XrHandJointLocationEXT, XrHandJointVelocityEXT>;

	std::array<joint, XR_HAND_JOINT_COUNT_EXT> locate(XrSpace space, XrTime time);
};
} // namespace xr
