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

#include "fb_body_tracker.h"
#include "spdlog/spdlog.h"
#include "xr/meta_body_tracking_fidelity.h"
#include "xr/xr.h"
#include <openxr/openxr.h>

static_assert(xr::pico_body_tracker::joint_whitelist.size() <= wivrn::from_headset::body_tracking::max_tracked_poses);

xr::fb_body_tracker::fb_body_tracker(instance & inst, session & s) :
        s(s)
{
	xrCreateBodyTrackerFB = inst.get_proc<PFN_xrCreateBodyTrackerFB>("xrCreateBodyTrackerFB");
	xrRequestBodyTrackingFidelityMETA = inst.get_proc<PFN_xrRequestBodyTrackingFidelityMETA>("xrRequestBodyTrackingFidelityMETA");
	xrLocateBodyJointsFB = inst.get_proc<PFN_xrLocateBodyJointsFB>("xrLocateBodyJointsFB");
	xrDestroyBodyTrackerFB = inst.get_proc<PFN_xrDestroyBodyTrackerFB>("xrDestroyBodyTrackerFB");
}
xr::fb_body_tracker::~fb_body_tracker()
{
	stop();
}

void xr::fb_body_tracker::start(bool full_body, bool hip)
{
	assert(xrCreateBodyTrackerFB);
	this->full_body = full_body;
	this->hip = hip;

	XrBodyTrackerCreateInfoFB create_info{
	        .type = XR_TYPE_BODY_TRACKER_CREATE_INFO_FB,
	        .next = nullptr,
	};
	create_info.bodyJointSet = full_body
	                                   ? XR_BODY_JOINT_SET_FULL_BODY_META
	                                   : XR_BODY_JOINT_SET_DEFAULT_FB;

	CHECK_XR(xrCreateBodyTrackerFB(s, &create_info, &handle));

	// Enable IOBT.
	CHECK_XR(xrRequestBodyTrackingFidelityMETA(handle, XR_BODY_TRACKING_FIDELITY_HIGH_META));
}
void xr::fb_body_tracker::stop()
{
	assert(xrDestroyBodyTrackerFB);
	full_body = false;
	if (handle)
		CHECK_XR(xrDestroyBodyTrackerFB(std::exchange(handle, nullptr)));
}

std::optional<std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses>> xr::fb_body_tracker::locate_spaces(XrTime time, XrSpace reference)
{
	if (!handle)
		return std::nullopt;

	assert(xrLocateBodyJointsFB);

	XrBodyJointsLocateInfoFB locate_info{
	        .type = XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB,
	        .next = nullptr,
	        .baseSpace = reference,
	        .time = time,
	};

	std::array<XrBodyJointLocationFB, XR_FULL_BODY_JOINT_COUNT_META> joints{};
	XrBodyJointLocationsFB joint_locations{
	        .type = XR_TYPE_BODY_JOINT_LOCATIONS_FB,
	        .next = nullptr,
	        .jointCount = (uint32_t)(full_body ? XR_FULL_BODY_JOINT_COUNT_META : (XrFullBodyJointMETA)XR_BODY_JOINT_COUNT_FB),
	        .jointLocations = joints.data(),
	};

	if (auto res = xrLocateBodyJointsFB(handle, &locate_info, &joint_locations); !XR_SUCCEEDED(res))
	{
		spdlog::warn("xrLocateBodyJointsFB returned {}", xr::to_string(res));
		return std::nullopt;
	}

	if (!joint_locations.isActive)
	{
		spdlog::warn("Body tracker is not active.");
		return std::nullopt;
	}

	std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses> poses{};
	int num_poses = 0;
	for (int i = 0; i < joint_whitelist.size(); i++)
	{
		auto joint = joint_whitelist[i];
		if (!full_body && (joint == XR_FULL_BODY_JOINT_HIPS_META || joint >= XR_FULL_BODY_JOINT_LEFT_UPPER_LEG_META))
			continue;
		if (!hip && joint == XR_FULL_BODY_JOINT_HIPS_META)
			continue;

		const auto & joint_loc = joints[joint];
		wivrn::from_headset::body_tracking::pose pose{
		        .pose = joint_loc.pose,
		        .flags = 0,
		};

		if (joint_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
		{
			pose.flags |= wivrn::from_headset::body_tracking::orientation_valid;
			pose.flags |= wivrn::from_headset::body_tracking::orientation_tracked;
		}
		if (joint_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		{
			pose.flags |= wivrn::from_headset::body_tracking::position_valid;
			pose.flags |= wivrn::from_headset::body_tracking::position_tracked;
		}

		poses[num_poses++] = pose;
	}
	return poses;
}
