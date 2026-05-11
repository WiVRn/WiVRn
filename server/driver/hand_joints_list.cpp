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

#include "hand_joints_list.h"
#include "math/m_space.h"
#include "pose_list.h"
#include "xrt_cast.h"

static_assert(XRT_HAND_JOINT_COUNT == XR_HAND_JOINT_COUNT_EXT);

namespace wivrn
{

static xrt_hand_joint_value interpolate(const xrt_hand_joint_value & a, const xrt_hand_joint_value & b, float t)
{
	return {
	        .relation = pose_list::interpolate(a.relation, b.relation, t),
	        .radius = a.radius * (1 - t) + b.radius * t,
	};
}

xrt_hand_joint_set hand_joints_list::interpolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, float t)
{
	xrt_hand_joint_set j = {
	        .hand_pose = pose_list::interpolate(a.hand_pose, b.hand_pose, t),
	        .is_active = a.is_active,
	};
	for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++)
	{
		j.values.hand_joint_set_default[i] = ::wivrn::interpolate(a.values.hand_joint_set_default[i], b.values.hand_joint_set_default[i], t);
	}
	return j;
}

xrt_hand_joint_set hand_joints_list::extrapolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, int64_t ta, int64_t tb, int64_t t)
{
	return t <= ta ? a : b;
}

static xrt_space_relation_flags cast_flags(uint8_t in_flags)
{
	std::underlying_type_t<xrt_space_relation_flags> flags = 0;
	if (in_flags & from_headset::hand_tracking::position_valid)
		flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (in_flags & from_headset::hand_tracking::orientation_valid)
		flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	if (in_flags & from_headset::hand_tracking::linear_velocity_valid)
		flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;

	if (in_flags & from_headset::hand_tracking::angular_velocity_valid)
		flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;

	if (in_flags & from_headset::hand_tracking::position_tracked)
		flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	if (in_flags & from_headset::hand_tracking::orientation_tracked)
		flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	return xrt_space_relation_flags(flags);
}

static xrt_space_relation to_relation(const from_headset::hand_tracking::pose & pose)
{
	return {
	        .relation_flags = cast_flags(pose.flags),
	        .pose = xrt_cast(XrPosef{
	                .orientation = pose.orientation,
	                .position = pose.position,
	        }),
	        .linear_velocity = xrt_cast(pose.linear_velocity),
	        .angular_velocity = xrt_cast(pose.angular_velocity),
	};
}

static xrt_hand_joint_set convert_joints(const std::optional<std::array<from_headset::hand_tracking::pose, XR_HAND_JOINT_COUNT_EXT>> & input_joints)
{
	xrt_hand_joint_set output_joints{};

	if (input_joints)
	{
		output_joints.is_active = true;
		output_joints.hand_pose = to_relation((*input_joints)[XRT_HAND_JOINT_WRIST]);

		xrt_relation_chain rel_chain{};
		xrt_space_relation * joint_rel = m_relation_chain_reserve(&rel_chain);
		m_relation_chain_push_inverted_relation(&rel_chain, &output_joints.hand_pose);

		for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
		{
			xrt_hand_joint_value & res = output_joints.values.hand_joint_set_default[i];

			res.radius = (*input_joints)[i].radius / 10'000.;
			*joint_rel = to_relation((*input_joints)[i]);

			m_relation_chain_resolve(&rel_chain, &res.relation);
		}
	}
	else
	{
		output_joints.is_active = false;
	}

	return output_joints;
}

void hand_joints_list::update_tracking(const from_headset::hand_tracking & tracking, const clock_offset & offset)
{
	if (tracking.hand == hand_id)
		add_sample(
		        tracking.production_timestamp,
		        tracking.timestamp,
		        convert_joints(tracking.joints),
		        offset);
}
} // namespace wivrn
