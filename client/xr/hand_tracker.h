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
XrResult destroy_hand_tracker(XrHandTrackerEXT);

class hand_tracker : public utils::handle<XrHandTrackerEXT, destroy_hand_tracker>
{
	PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT{};

public:
	hand_tracker() = default;
	hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info);

	using joint = std::pair<XrHandJointLocationEXT, XrHandJointVelocityEXT>;

	std::optional<std::array<joint, XR_HAND_JOINT_COUNT_EXT>> locate(XrSpace space, XrTime time);

	static bool check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity);
};
} // namespace xr
