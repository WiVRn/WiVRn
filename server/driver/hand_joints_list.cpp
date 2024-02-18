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
#include "pose_list.h"
#include "xrt_cast.h"

using namespace xrt::drivers::wivrn;

static_assert(XRT_HAND_JOINT_COUNT == XR_HAND_JOINT_COUNT_EXT);

namespace
{
	xrt_hand_joint_value interpolate(const xrt_hand_joint_value & a, const xrt_hand_joint_value & b, float t)
	{
		return {
			.relation = pose_list::interpolate(a.relation, b.relation, t),
			.radius = a.radius * (1-t) + b.radius * t
		};
	}

	xrt_hand_joint_value extrapolate(const xrt_hand_joint_value & a, const xrt_hand_joint_value & b, uint64_t ta, uint64_t tb, uint64_t t)
	{
		float λ = std::clamp((t - ta) / float(tb - ta), 0.f, 1.f);

		return {
			.relation = pose_list::extrapolate(a.relation, b.relation, ta, tb, t),
			.radius = a.radius * (1-λ) + b.radius * λ
		};
	}
}

xrt_hand_joint_set hand_joints_list::interpolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, float t)
{
	xrt_hand_joint_set j = a;
	for(int i = 0; i < XRT_HAND_JOINT_COUNT; i++)
	{
		j.values.hand_joint_set_default[i] = ::interpolate(a.values.hand_joint_set_default[i], b.values.hand_joint_set_default[i], t);
	}
	return j;
}

xrt_hand_joint_set hand_joints_list::extrapolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, uint64_t ta, uint64_t tb, uint64_t t)
{
	xrt_hand_joint_set j = a;
	for(int i = 0; i < XRT_HAND_JOINT_COUNT; i++)
	{
		j.values.hand_joint_set_default[i] = ::extrapolate(a.values.hand_joint_set_default[i], b.values.hand_joint_set_default[i], ta, tb, t);
	}
	return j;
}

static xrt_hand_joint_set convert_joints(const std::array<from_headset::hand_tracking::pose, XR_HAND_JOINT_COUNT_EXT>& input_joints)
{
	xrt_hand_joint_set output_joints;

	output_joints.is_active = true;
	output_joints.hand_pose = xrt_space_relation{
		.relation_flags = XRT_SPACE_RELATION_BITMASK_ALL,
		.pose = {
			.orientation = {0, 0, 0, 1},
			.position = {0, 0, 0}
		},
		.linear_velocity = {0, 0, 0},
		.angular_velocity = {0, 0, 0},
	}; // TODO

	for(int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		xrt_hand_joint_value& res = output_joints.values.hand_joint_set_default[i];

		res.radius = input_joints[i].radius;
		res.relation.pose = xrt_cast(input_joints[i].pose);
		res.relation.angular_velocity = xrt_cast(input_joints[i].angular_velocity);
		res.relation.linear_velocity = xrt_cast(input_joints[i].linear_velocity);

		int flags = 0;
		if (input_joints[i].flags & from_headset::hand_tracking::position_valid)
			flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;

		if (input_joints[i].flags & from_headset::hand_tracking::orientation_valid)
			flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

		if (input_joints[i].flags & from_headset::hand_tracking::linear_velocity_valid)
			flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;

		if (input_joints[i].flags & from_headset::hand_tracking::angular_velocity_valid)
			flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;

		if (input_joints[i].flags & from_headset::hand_tracking::position_tracked)
			flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

		if (input_joints[i].flags & from_headset::hand_tracking::orientation_tracked)
			flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;

		res.relation.relation_flags = (xrt_space_relation_flags)flags;
	}
	return output_joints;
}

void hand_joints_list::update_tracking(const from_headset::hand_tracking & tracking, const clock_offset & offset)
{
	if (hand_id == 0)
		add_sample(tracking.timestamp, convert_joints(tracking.left), offset);
	else
		add_sample(tracking.timestamp, convert_joints(tracking.right), offset);
}
