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
	CHECK_XR(xrCreateHandTrackerEXT(session, &info, &id));
}

std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> xr::hand_tracker::locate(XrSpace space, XrTime time)
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

	XrHandJointVelocitiesEXT velocities{
	        .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
	        .next = nullptr,
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

	std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> joints;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		joints[i] = {joints_pos[i], joints_vel[i]};
	}

	return joints;
}

bool xr::hand_tracker::check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity)
{
	return std::ranges::all_of(joints, [position, velocity](const auto & joint) {
		return (joint.first.locationFlags & position) == position and (joint.second.velocityFlags & velocity) == velocity;
	});
}
