/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "pose_list.h"
#include "math/m_eigen_interop.hpp"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "xrt/xrt_defines.h"
#include "xrt_cast.h"

using namespace xrt::auxiliary::math;

xrt_space_relation interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t)
{
	xrt_space_relation result;
	xrt_space_relation_flags flags = xrt_space_relation_flags(a.relation_flags & b.relation_flags);
	m_space_relation_interpolate(const_cast<xrt_space_relation *>(&a), const_cast<xrt_space_relation *>(&b), t, flags, &result);
	return result;
}

xrt_space_relation extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, uint64_t ta, uint64_t tb, uint64_t t)
{
	if (t < ta)
		return a;

	float h = (tb - ta) / 1.e9;

	xrt_space_relation res;
	res.relation_flags = b.relation_flags;

	xrt_vec3 lin_acc = (b.linear_velocity - a.linear_velocity) / h;
	xrt_vec3 ang_acc = (b.angular_velocity - a.angular_velocity) / h;

	float dt = (t - tb) / 1.e9;
	// if (dt > 0.2)
	// dt = 0.2; // saturate dt to 200ms

	float dt2_over_2 = dt * dt / 2;
	res.linear_velocity = b.linear_velocity + lin_acc * dt;
	res.pose.position = b.pose.position + b.linear_velocity * dt + lin_acc * dt2_over_2;

	res.angular_velocity = b.angular_velocity + ang_acc * dt;
	xrt_vec3 dtheta = b.angular_velocity * dt + ang_acc * dt2_over_2;
	xrt_quat dq;
	math_quat_exp(&dtheta, &dq);

	map_quat(res.pose.orientation) = map_quat(b.pose.orientation) * map_quat(dq);

	return res;
}

void pose_list::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device)
			continue;

		add_sample(tracking.timestamp, convert_pose(pose), offset);
		return;
	}
}

xrt_space_relation pose_list::convert_pose(const from_headset::tracking::pose & pose)
{
	xrt_space_relation res{};

	res.pose = xrt_cast(pose.pose);
	res.angular_velocity = xrt_cast(pose.angular_velocity);
	res.linear_velocity = xrt_cast(pose.linear_velocity);

	int flags = 0;
	if (pose.flags & from_headset::tracking::position_valid)
		flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (pose.flags & from_headset::tracking::orientation_valid)
		flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	if (pose.flags & from_headset::tracking::linear_velocity_valid)
		flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;

	if (pose.flags & from_headset::tracking::angular_velocity_valid)
		flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;

	if (pose.flags & from_headset::tracking::position_tracked)
		flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	if (pose.flags & from_headset::tracking::orientation_tracked)
		flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;

	res.relation_flags = (xrt_space_relation_flags)flags;

	return res;
}
