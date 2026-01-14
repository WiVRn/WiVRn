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
#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "xrt/xrt_defines.h"
#include "xrt_cast.h"

using namespace xrt::auxiliary::math;

namespace wivrn
{

xrt_space_relation pose_list::interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t)
{
	xrt_space_relation result;
	xrt_space_relation_flags flags = xrt_space_relation_flags(a.relation_flags & b.relation_flags);

	if (math_quat_dot(&a.pose.orientation, &b.pose.orientation) > 0)
	{
		m_space_relation_interpolate(const_cast<xrt_space_relation *>(&a), const_cast<xrt_space_relation *>(&b), t, flags, &result);
	}
	else
	{
		xrt_space_relation b2{
		        .relation_flags = b.relation_flags,
		        .pose = {
		                .orientation = {
		                        .x = -b.pose.orientation.x,
		                        .y = -b.pose.orientation.y,
		                        .z = -b.pose.orientation.z,
		                        .w = -b.pose.orientation.w,
		                },
		                .position = b.pose.position,
		        },
		        .linear_velocity = b.linear_velocity,
		        .angular_velocity = b.angular_velocity,
		};

		m_space_relation_interpolate(const_cast<xrt_space_relation *>(&a), &b2, t, flags, &result);
	}
	return result;
}

xrt_space_relation pose_list::extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t)
{
	float h = (tb - ta) / 1.e9;

	xrt_space_relation res = t < ta ? a : b;

	xrt_vec3 lin_vel = res.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT ? res.linear_velocity : (b.pose.position - a.pose.position) / h;

	float dt = (t - tb) / 1.e9;

	float dt2_over_2 = dt * dt / 2;
	res.pose.position = res.pose.position + lin_vel * dt;

	if (res.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT)
	{
		xrt_vec3 dtheta = res.angular_velocity * dt;
		xrt_quat dq;
		math_quat_exp(&dtheta, &dq);

		map_quat(res.pose.orientation) = map_quat(res.pose.orientation) * map_quat(dq);
	}

	return res;
}

bool pose_list::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	if (source)
		return false;

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device)
			continue;

		return add_sample(tracking.production_timestamp, tracking.timestamp, convert_pose(pose), offset);
	}
	return true;
}

void pose_list::set_derived(pose_list * source, xrt_pose offset, bool force)
{
	if (force)
	{
		derive_forced = true;
	}
	else if (derive_forced)
	{
		return;
	}
	// TODO: check for loops?
	if (source == this)
		this->source = nullptr;
	else
	{
		this->offset = offset;
		this->source = source;
	}
}

std::tuple<std::chrono::nanoseconds, xrt_space_relation, device_id> pose_list::get_pose_at(XrTime at_timestamp_ns)
{
	if (auto source = this->source.load())
	{
		auto res = source->get_pose_at(at_timestamp_ns);
		math_pose_transform(&std::get<1>(res).pose, &offset, &std::get<1>(res).pose);
		return res;
	}

	return std::tuple_cat(get_at(at_timestamp_ns), std::make_tuple(device));
}

static xrt_space_relation_flags convert_flags(uint8_t flags)
{
	static_assert(int(from_headset::tracking::position_valid) == XRT_SPACE_RELATION_POSITION_VALID_BIT);
	static_assert(int(from_headset::tracking::orientation_valid) == XRT_SPACE_RELATION_ORIENTATION_VALID_BIT);
	static_assert(int(from_headset::tracking::linear_velocity_valid) == XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
	static_assert(int(from_headset::tracking::angular_velocity_valid) == XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
	static_assert(int(from_headset::tracking::orientation_tracked) == XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	static_assert(int(from_headset::tracking::position_tracked) == XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
	return xrt_space_relation_flags(flags);
}

xrt_space_relation pose_list::convert_pose(const from_headset::tracking::pose & pose)
{
	return xrt_space_relation{
	        .relation_flags = convert_flags(pose.flags),
	        .pose = xrt_cast(pose.pose),
	        .linear_velocity = xrt_cast(pose.linear_velocity),
	        .angular_velocity = xrt_cast(pose.angular_velocity),
	};
}
} // namespace wivrn
