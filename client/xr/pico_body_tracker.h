/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
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
#include "wivrn_packets.h"

#include <vector>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

XrResult destroy_pico_body_tracker(XrBodyTrackerBD);

class pico_body_tracker : public utils::handle<XrBodyTrackerBD, destroy_pico_body_tracker>
{
	PFN_xrLocateBodyJointsBD xrLocateBodyJointsBD{};

public:
	static constexpr std::array joint_whitelist{
	        XR_BODY_JOINT_PELVIS_BD,
	        XR_BODY_JOINT_LEFT_SHOULDER_BD,
	        XR_BODY_JOINT_RIGHT_SHOULDER_BD,
	        XR_BODY_JOINT_LEFT_ELBOW_BD,
	        XR_BODY_JOINT_RIGHT_ELBOW_BD,

	        XR_BODY_JOINT_LEFT_KNEE_BD,
	        XR_BODY_JOINT_RIGHT_KNEE_BD,
	        XR_BODY_JOINT_LEFT_FOOT_BD,
	        XR_BODY_JOINT_RIGHT_FOOT_BD,
	};

	pico_body_tracker() = default;
	pico_body_tracker(instance & inst, XrBodyTrackerBD h);

	void locate_spaces(XrTime time, std::vector<wivrn::from_headset::tracking::pose> & out_poses, XrSpace reference);
};
} // namespace xr
