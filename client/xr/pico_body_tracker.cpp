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

static PFN_xrDestroyBodyTrackerBD xrDestroyBodyTrackerBD{};

XrResult xr::destroy_pico_body_tracker(XrBodyTrackerBD id)
{
	return xrDestroyBodyTrackerBD(id);
}

xr::pico_body_tracker::pico_body_tracker(instance & inst, XrBodyTrackerBD h)
{
	id = h;
	xrLocateBodyJointsBD = inst.get_proc<PFN_xrLocateBodyJointsBD>("xrLocateBodyJointsBD");
}

void xr::pico_body_tracker::locate_spaces(XrTime time, std::vector<wivrn::from_headset::tracking::pose> & out_poses, XrSpace reference)
{
	auto fill_poses = [&]() {
		for (auto _: joint_whitelist)
		{
			wivrn::from_headset::tracking::pose pose{
			        .pose = {},
			        .device = wivrn::device_id::GENERIC_TRACKER,
			        .flags = 0,
			};
			out_poses.push_back(std::move(pose));
		}
	};

	if (!xrLocateBodyJointsBD)
	{
		fill_poses();
		return;
	}

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
		fill_poses();
		return;
	}

	for (auto & joint: joint_whitelist)
	{
		auto & joint_location = joints[joint];
		wivrn::from_headset::tracking::pose pose{
		        .pose = joint_location.pose,
		        .device = wivrn::device_id::GENERIC_TRACKER,
		        .flags = 0,
		};

		if (joint_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
			pose.flags |= wivrn::from_headset::tracking::position_valid;
		if (joint_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
			pose.flags |= wivrn::from_headset::tracking::orientation_valid;
		if (joint_location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
			pose.flags |= wivrn::from_headset::tracking::position_tracked;
		if (joint_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
			pose.flags |= wivrn::from_headset::tracking::orientation_tracked;

		out_poses.push_back(std::move(pose));
	}
}
