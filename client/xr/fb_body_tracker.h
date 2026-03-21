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

#include "utils/handle.h"
#include "xr/meta_body_tracking_fidelity.h"
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class fb_body_tracker : public utils::handle<XrBodyTrackerFB>
{
	PFN_xrRequestBodyTrackingFidelityMETA xrRequestBodyTrackingFidelityMETA{};
	PFN_xrLocateBodyJointsFB xrLocateBodyJointsFB{};
	PFN_xrGetBodySkeletonFB xrGetBodySkeletonFB{};

	XrBodyJointSetFB joint_set{};
	uint32_t last_sent_skeleton_generation = static_cast<uint32_t>(-1);
	uint32_t skeleton_generation = 0;

public:
	using packet_type = wivrn::from_headset::meta_body;

	fb_body_tracker(instance & inst, session & s, bool lower_body);

	packet_type locate_spaces(XrTime time, XrSpace reference);
	bool should_send_skeleton();
	wivrn::from_headset::meta_body_skeleton get_skeleton() noexcept(false);
};
} // namespace xr
