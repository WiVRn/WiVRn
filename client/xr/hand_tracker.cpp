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

static PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT{};

XrResult xr::destroy_hand_tracker(XrHandTrackerEXT id)
{
	return xrDestroyHandTrackerEXT(id);
}

xr::hand_tracker::hand_tracker(instance & inst, XrHandTrackerEXT h)
{
	id = h;
	xrLocateHandJointsEXT = inst.get_proc<PFN_xrLocateHandJointsEXT>("xrLocateHandJointsEXT");
	xrDestroyHandTrackerEXT = inst.get_proc<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT");
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

	std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> joints;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		joints[i] = {joints_pos[i], joints_vel[i]};
	}

	return joints;
}
