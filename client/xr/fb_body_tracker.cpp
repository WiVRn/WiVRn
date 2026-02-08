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
#include "xr/instance.h"
#include "xr/meta_body_tracking_fidelity.h"
#include "xr/session.h"
#include "xr/to_string.h"
#include <openxr/openxr.h>

static uint8_t convert_flags(XrSpaceLocationFlags in_flags)
{
	uint8_t flags{};
	if (in_flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
		flags |= wivrn::from_headset::meta_body::orientation_valid;
	if (in_flags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		flags |= wivrn::from_headset::meta_body::position_valid;
	if (in_flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
		flags |= wivrn::from_headset::meta_body::orientation_tracked;
	if (in_flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
		flags |= wivrn::from_headset::meta_body::position_tracked;

	return flags;
}

xr::fb_body_tracker::fb_body_tracker(instance & inst, session & s) :
        handle(inst.get_proc<PFN_xrDestroyBodyTrackerFB>("xrDestroyBodyTrackerFB")),
        xrRequestBodyTrackingFidelityMETA(inst.get_proc<PFN_xrRequestBodyTrackingFidelityMETA>("xrRequestBodyTrackingFidelityMETA")),
        xrLocateBodyJointsFB(inst.get_proc<PFN_xrLocateBodyJointsFB>("xrLocateBodyJointsFB"))
{
	auto xrCreateBodyTrackerFB = inst.get_proc<PFN_xrCreateBodyTrackerFB>("xrCreateBodyTrackerFB");

	XrBodyTrackerCreateInfoFB create_info{
	        .type = XR_TYPE_BODY_TRACKER_CREATE_INFO_FB,
	        .bodyJointSet = XR_BODY_JOINT_SET_FULL_BODY_META,
	};

	CHECK_XR(xrCreateBodyTrackerFB(s, &create_info, &id));

	// Enable IOBT.
	CHECK_XR(xrRequestBodyTrackingFidelityMETA(id, XR_BODY_TRACKING_FIDELITY_HIGH_META));
}

xr::fb_body_tracker::packet_type xr::fb_body_tracker::locate_spaces(XrTime time, XrSpace reference)
{
	xr::fb_body_tracker::packet_type ret{};
	if (!id)
		return ret;

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
	        .jointCount = XR_FULL_BODY_JOINT_COUNT_META,
	        .jointLocations = joints.data(),
	};

	if (auto res = xrLocateBodyJointsFB(id, &locate_info, &joint_locations); !XR_SUCCEEDED(res))
	{
		spdlog::warn("xrLocateBodyJointsFB returned {}", xr::to_string(res));
		return ret;
	}

	if (!joint_locations.isActive)
	{
		return ret;
	}

	ret.confidence = joint_locations.confidence;
	const auto & base = joints[XR_FULL_BODY_JOINT_ROOT_META];
	ret.base.emplace(base.pose.position, pack(base.pose.orientation), convert_flags(base.locationFlags));
	auto & out_joints = ret.joints.emplace();
	for (size_t joint = XR_FULL_BODY_JOINT_HIPS_META; joint < XR_FULL_BODY_JOINT_COUNT_META; joint++)
	{
		const auto & loc = joints[joint];
		out_joints[joint - 1] = {
		        .position = {
		                .x = int16_t((loc.pose.position.x - base.pose.position.x) * 10'000.f),
		                .y = int16_t((loc.pose.position.y - base.pose.position.y) * 10'000.f),
		                .z = int16_t((loc.pose.position.z - base.pose.position.z) * 10'000.f)},
		        .orientation = pack(loc.pose.orientation),
		        .flags = convert_flags(loc.locationFlags),
		};
	}
	return ret;
}
