/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_generic_tracker.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "wivrn_session.h"

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_results.h"
#include "xrt_cast.h"
#include <chrono>
#include <format>

using namespace xrt::auxiliary::math;

namespace wivrn
{

xrt_space_relation tracker_pose_list::interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t)
{
	xrt_space_relation result;
	xrt_space_relation_flags flags = xrt_space_relation_flags(a.relation_flags & b.relation_flags);
	m_space_relation_interpolate(const_cast<xrt_space_relation *>(&a), const_cast<xrt_space_relation *>(&b), t, flags, &result);
	return result;
}

xrt_space_relation tracker_pose_list::extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t)
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

bool tracker_pose_list::update_tracking(XrTime produced_timestamp, XrTime timestamp, const from_headset::body_tracking::pose & pose, const clock_offset & offset)
{
	return add_sample(produced_timestamp, timestamp, convert_pose(pose), offset);
}

std::pair<std::chrono::nanoseconds, xrt_space_relation> tracker_pose_list::get_pose_at(XrTime at_timestamp_ns)
{
	return get_at(at_timestamp_ns);
}

static xrt_space_relation_flags convert_flags(uint8_t flags)
{
	std::underlying_type_t<xrt_space_relation_flags> out_flags = 0;
	if (flags & from_headset::body_tracking::orientation_valid)
		out_flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
	if (flags & from_headset::body_tracking::position_valid)
		out_flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (flags & from_headset::body_tracking::orientation_tracked)
		out_flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	if (flags & from_headset::body_tracking::position_tracked)
		out_flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	return xrt_space_relation_flags(out_flags);
}

xrt_space_relation tracker_pose_list::convert_pose(const from_headset::body_tracking::pose & pose)
{
	return xrt_space_relation{
	        .relation_flags = convert_flags(pose.flags),
	        .pose = xrt_cast(pose.pose),
	        .linear_velocity = {},
	        .angular_velocity = {},
	};
}

wivrn_generic_tracker::wivrn_generic_tracker(int index, xrt_device * hmd, wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_VIVE_TRACKER,
                .device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER,
                .hmd = nullptr,
                .tracking_origin = hmd->tracking_origin,
                .supported = {
                        .orientation_tracking = true,
                        .position_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_generic_tracker::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_generic_tracker::get_tracked_pose>,
                .destroy = [](xrt_device *) {},
        },
        cnx(cnx),
        index(index)
{
	auto unique_name = std::format("WiVRn Generic Tracker #{}", index + 1);
	strlcpy(str, unique_name.c_str(), std::size(str));
	strlcpy(serial, unique_name.c_str(), std::size(serial));

	pose_input.name = XRT_INPUT_GENERIC_TRACKER_POSE;
	pose_input.active = true;

	inputs = &pose_input;
	input_count = 1;
}

#define XRT_INPUT_NAME_CASE(NAME, VALUE) \
	case VALUE:                      \
		return #NAME;

const char * input_name_str(xrt_input_name name)
{
	switch (name)
	{
		XRT_INPUT_LIST(XRT_INPUT_NAME_CASE)
		default:
			return "Unknown";
	}
}

xrt_result_t wivrn_generic_tracker::update_inputs()
{
	return XRT_SUCCESS;
}
xrt_result_t wivrn_generic_tracker::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * res)
{
	std::chrono::nanoseconds extrapolation_time;

	if (name == XRT_INPUT_GENERIC_TRACKER_POSE)
	{
		std::tie(extrapolation_time, *res) = poses.get_pose_at(at_timestamp_ns);

		cnx.set_tracker_enabled(index, true);
		cnx.add_predict_offset(extrapolation_time);
		return XRT_SUCCESS;
	}

	U_LOG_D("Unknown input name %s", input_name_str(name));
	return XRT_ERROR_NOT_IMPLEMENTED;
}

void wivrn_generic_tracker::update_tracking(const from_headset::body_tracking & tracking, const from_headset::body_tracking::pose & pose, const clock_offset & offset)
{
	if (!poses.update_tracking(tracking.production_timestamp, tracking.timestamp, pose, offset))
		cnx.set_tracker_enabled(index, false);
}
} // namespace wivrn
