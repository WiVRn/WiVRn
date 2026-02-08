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

#include "pico_body_tracker.h"
#include "spdlog/spdlog.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/to_string.h"
#include <openxr/openxr.h>

xr::pico_body_tracker::pico_body_tracker(instance & inst, session & s) :
        handle(inst.get_proc<PFN_xrDestroyBodyTrackerBD>("xrDestroyBodyTrackerBD"))
{
	auto xrCreateBodyTrackerBD = inst.get_proc<PFN_xrCreateBodyTrackerBD>("xrCreateBodyTrackerBD");
	xrLocateBodyJointsBD = inst.get_proc<PFN_xrLocateBodyJointsBD>("xrLocateBodyJointsBD");
	assert(xrCreateBodyTrackerBD);

	XrBodyTrackerCreateInfoBD create_info{
	        .type = XR_TYPE_BODY_TRACKER_CREATE_INFO_BD,
	        .next = nullptr,
	        .jointSet = XR_BODY_JOINT_SET_FULL_BODY_JOINTS_BD,
	};

	CHECK_XR(xrCreateBodyTrackerBD(s, &create_info, &id));
}

xr::pico_body_tracker::packet_type xr::pico_body_tracker::locate_spaces(XrTime time, XrSpace reference)
{
	xr::pico_body_tracker::packet_type ret{};
	if (!xrLocateBodyJointsBD)
		return ret;

	XrBodyJointsLocateInfoBD locate_info{
	        .type = XR_TYPE_BODY_JOINTS_LOCATE_INFO_BD,
	        .next = nullptr,
	        .baseSpace = reference,
	        .time = time,
	};

	std::array<XrBodyJointLocationBD, XR_BODY_JOINT_COUNT_BD> joints{};
	XrBodyJointLocationsBD joint_locations{
	        .type = XR_TYPE_BODY_JOINT_LOCATIONS_BD,
	        .next = nullptr,
	        .jointLocationCount = joints.size(),
	        .jointLocations = joints.data(),
	};

	if (auto res = xrLocateBodyJointsBD(id, &locate_info, &joint_locations); !XR_SUCCEEDED(res))
	{
		spdlog::warn("Unable to get body joints: xrLocateBodyJointsBD returned {}", xr::to_string(res));
		return ret;
	}

	ret.all_tracked = joint_locations.allJointPosesTracked;
	for (size_t joint = XR_BODY_JOINT_PELVIS_BD; joint < XR_BODY_JOINT_COUNT_BD; joint++)
	{
		auto & joint_loc = joints[joint];
		ret.joints[joint] = {
		        .position = joint_loc.pose.position,
		        .orientation = pack(joint_loc.pose.orientation),
		};
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
			ret.joints[joint].flags |= wivrn::from_headset::bd_body::orientation_valid;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
			ret.joints[joint].flags |= wivrn::from_headset::bd_body::position_valid;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
			ret.joints[joint].flags |= wivrn::from_headset::bd_body::orientation_tracked;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
			ret.joints[joint].flags |= wivrn::from_headset::bd_body::position_tracked;
	}

	return ret;
}
