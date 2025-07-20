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
#include "xr/xr.h"
#include <openxr/openxr.h>

static_assert(xr::pico_body_tracker::joint_whitelist.size() <= wivrn::from_headset::body_tracking::max_tracked_poses);

static PFN_xrDestroyBodyTrackerBD xrDestroyBodyTrackerBD{};

XrResult xr::destroy_pico_body_tracker(XrBodyTrackerBD id)
{
	return xrDestroyBodyTrackerBD(id);
}

xr::pico_body_tracker::pico_body_tracker(instance & inst, session & s)
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

size_t xr::pico_body_tracker::count() const
{
	return joint_whitelist.size();
}
std::optional<std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses>> xr::pico_body_tracker::locate_spaces(XrTime time, XrSpace reference)
{
	if (!xrLocateBodyJointsBD)
		return std::nullopt;

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
		return std::nullopt;
	}

	std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses> poses{};
	int num_poses = 0;
	for (auto joint: joint_whitelist)
	{
		const auto & joint_loc = joints[joint];
		wivrn::from_headset::body_tracking::pose pose{
		        .pose = joint_loc.pose,
		        .flags = 0,
		};

		if (joint_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
			pose.flags |= wivrn::from_headset::body_tracking::orientation_valid;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
			pose.flags |= wivrn::from_headset::body_tracking::position_valid;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
			pose.flags |= wivrn::from_headset::body_tracking::orientation_tracked;
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
			pose.flags |= wivrn::from_headset::body_tracking::position_tracked;

		poses[num_poses++] = pose;
	}
	return poses;
}
