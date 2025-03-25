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
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.inl>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "hardware.h"

static PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT{};

XrResult xr::destroy_hand_tracker(XrHandTrackerEXT id)
{
	return xrDestroyHandTrackerEXT(id);
}

xr::hand_tracker::hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info)
{
	hand_id = info.hand;
	
	if (hand_id == XrHandEXT::XR_HAND_LEFT_EXT)
	{
		offset_angle = glm::radians(-90.0f);
	}
	if (hand_id == XrHandEXT::XR_HAND_RIGHT_EXT)
	{
		offset_angle = glm::radians(90.0f);
	}

	static auto xrCreateHandTrackerEXT = inst.get_proc<PFN_xrCreateHandTrackerEXT>("xrCreateHandTrackerEXT");
	assert(xrCreateHandTrackerEXT);
	xrLocateHandJointsEXT = inst.get_proc<PFN_xrLocateHandJointsEXT>("xrLocateHandJointsEXT");
	xrDestroyHandTrackerEXT = inst.get_proc<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT");
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


	switch (guess_model())
	{
		case model::meta_quest_3:
		case model::meta_quest_pro:
		case model::meta_quest_3s:
		case model::oculus_quest:
		case model::oculus_quest_2:
			if (hand_id == XR_HAND_LEFT_EXT)
				offset_angle = glm::radians(-90.0f);
			else if (hand_id == XR_HAND_RIGHT_EXT)
				offset_angle = glm::radians(90.0f);
			break;
		
		default:
			offset_angle = 0.0f;
			break;
	}

	std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> joints;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{

		if (i >= XR_HAND_JOINT_THUMB_METACARPAL_EXT && i <= XR_HAND_JOINT_THUMB_TIP_EXT)
		{

			// Need to convert the XrQuaternionf to a glm::quat to use glm::rotate
			glm::quat q(
				joints_pos[i].pose.orientation.w,
				joints_pos[i].pose.orientation.x,
				joints_pos[i].pose.orientation.y,
				joints_pos[i].pose.orientation.z);

			glm::quat offset_rotation = glm::rotate(q,offset_angle, glm::vec3(0,0,1));

			joints_pos[i].pose.orientation = {
				.x = offset_rotation.x,
				.y = offset_rotation.y,
				.z = offset_rotation.z,
				.w = offset_rotation.w,
			};

			joints[i] = {joints_pos[i], joints_vel[i]};
		}
		else
		{
			joints[i] = {joints_pos[i], joints_vel[i]};
		}

	}

	return joints;
}

bool xr::hand_tracker::check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity)
{
	return std::ranges::all_of(joints, [position, velocity](const auto & joint) {
		return (joint.first.locationFlags & position) == position and (joint.second.velocityFlags & velocity) == velocity;
	});
}
