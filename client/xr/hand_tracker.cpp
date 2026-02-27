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

#include "hand_tracker.h"
#include "xr/check.h"
#include "xr/instance.h"
#include "xr/session.h"
#include <cassert>

xr::hand_tracker::hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info) :
        handle(inst.get_proc<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT"))
{
	auto xrCreateHandTrackerEXT = inst.get_proc<PFN_xrCreateHandTrackerEXT>("xrCreateHandTrackerEXT");
	assert(xrCreateHandTrackerEXT);
	xrLocateHandJointsEXT = inst.get_proc<PFN_xrLocateHandJointsEXT>("xrLocateHandJointsEXT");
	aim_supported = inst.has_extension(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
	CHECK_XR(xrCreateHandTrackerEXT(session, &info, &id));
}

std::optional<xr::hand_tracker::locate_result> xr::hand_tracker::locate(XrSpace space, XrTime time)
{
	if (!id || !xrLocateHandJointsEXT)
		return std::nullopt;

	XrHandJointsLocateInfoEXT info{
	        .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
	        .next = nullptr,
	        .baseSpace = space,
	        .time = time,
	};

	std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints_pos;
	std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> joints_vel;

#ifndef NDEBUG
	// Silence the OpenXR validation layer by setting valid flags
	for (auto & i: joints_pos)
		i.locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT;
	for (auto & i: joints_vel)
		i.velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
#endif

	XrHandTrackingAimStateFB aim_state_fb{
	        .type = XR_TYPE_HAND_TRACKING_AIM_STATE_FB,
	        .next = nullptr,
	};

	XrHandJointVelocitiesEXT velocities{
	        .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
	        .next = aim_supported ? &aim_state_fb : nullptr,
	        .jointCount = joints_vel.size(),
	        .jointVelocities = joints_vel.data(),
	};

	XrHandJointLocationsEXT locations{
	        .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
	        .next = &velocities,
	        .jointCount = joints_pos.size(),
	        .jointLocations = joints_pos.data(),
	};

	CHECK_XR(xrLocateHandJointsEXT(id, &info, &locations));

	if (!locations.isActive)
		return std::nullopt;

	// bail if any of the joint is invalid
	if (std::ranges::any_of(joints_pos, [](const auto & loc) { return loc.locationFlags == 0; }))
		return std::nullopt;

	locate_result result;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		result.joints[i] = {joints_pos[i], joints_vel[i]};
	}

	// Extract aim state if available
	if (aim_supported && (aim_state_fb.status & XR_HAND_TRACKING_AIM_VALID_BIT_FB))
	{
		result.aim = aim_state{
		        .status = aim_state_fb.status,
		        .aim_pose = aim_state_fb.aimPose,
		        .pinch_strength_index = aim_state_fb.pinchStrengthIndex,
		        .pinch_strength_middle = aim_state_fb.pinchStrengthMiddle,
		        .pinch_strength_ring = aim_state_fb.pinchStrengthRing,
		        .pinch_strength_little = aim_state_fb.pinchStrengthLittle,
		};
	}

	return result;
}

bool xr::hand_tracker::check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity)
{
	return std::ranges::all_of(joints, [position, velocity](const auto & joint) {
		return (joint.first.locationFlags & position) == position and (joint.second.velocityFlags & velocity) == velocity;
	});
}

bool xr::hand_tracker::check_flags(const locate_result & result, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity)
{
	return check_flags(result.joints, position, velocity);
}
