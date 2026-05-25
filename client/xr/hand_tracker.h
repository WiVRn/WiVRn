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
#include <array>
#include <optional>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class hand_tracker : public utils::handle<XrHandTrackerEXT>
{
	PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT{};
	bool aim_supported{};

public:
	hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info);

	using joint = std::pair<XrHandJointLocationEXT, XrHandJointVelocityEXT>;

	struct aim_state
	{
		XrHandTrackingAimFlagsFB status;
		XrPosef aim_pose;
		float pinch_strength_index;
		float pinch_strength_middle;
		float pinch_strength_ring;
		float pinch_strength_little;
	};

	struct locate_result
	{
		std::array<joint, XR_HAND_JOINT_COUNT_EXT> joints;
		std::optional<aim_state> aim;
	};

	std::optional<locate_result> locate(XrSpace space, XrTime time);

	static bool check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity);
	static bool check_flags(const locate_result & result, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity);
};
} // namespace xr
