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

#include "wivrn_packets.h"

#include "xr/meta_body_tracking_fidelity.h"
#include <array>
#include <optional>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class fb_body_tracker
{
	XrSession s;
	XrBodyTrackerFB handle{};

	PFN_xrCreateBodyTrackerFB xrCreateBodyTrackerFB{};
	PFN_xrRequestBodyTrackingFidelityMETA xrRequestBodyTrackingFidelityMETA{};
	PFN_xrLocateBodyJointsFB xrLocateBodyJointsFB{};
	PFN_xrDestroyBodyTrackerFB xrDestroyBodyTrackerFB{};

	bool full_body{};
	bool hip{};
	std::vector<XrFullBodyJointMETA> whitelisted_joints{};

public:
	static constexpr std::array joint_whitelist{
	        XR_FULL_BODY_JOINT_HIPS_META,
	        XR_FULL_BODY_JOINT_CHEST_META,
	        XR_FULL_BODY_JOINT_LEFT_ARM_LOWER_META,
	        XR_FULL_BODY_JOINT_RIGHT_ARM_LOWER_META,

	        XR_FULL_BODY_JOINT_LEFT_LOWER_LEG_META,
	        XR_FULL_BODY_JOINT_RIGHT_LOWER_LEG_META,

	        XR_FULL_BODY_JOINT_LEFT_FOOT_TRANSVERSE_META,
	        XR_FULL_BODY_JOINT_RIGHT_FOOT_TRANSVERSE_META,
	};
	static std::vector<XrFullBodyJointMETA> get_whitelisted_joints(bool full_body, bool hip);

	fb_body_tracker() = default;
	fb_body_tracker(instance & inst, session & s);
	~fb_body_tracker();

	void start(bool lower_body, bool hip);
	void stop();
	size_t count() const;

	std::optional<std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses>> locate_spaces(XrTime time, XrSpace reference);
};
} // namespace xr
