/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2024  galister <galister@librevr.org>
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

#include "wivrn_htc_xr_tracker.h"

#include "driver/wivrn_session.h"
#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt_cast.h"

#include "util/u_logging.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <openxr/openxr.h>

namespace wivrn
{

static void
wivrn_xr_tracker_destroy(xrt_device * xdev);

static xrt_result_t wivrn_xr_tracker_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_xr_tracker *>(xdev)->update_inputs();
	return XRT_SUCCESS;
}

static xrt_result_t wivrn_xr_tracker_get_tracked_pose(xrt_device * xdev,
                                                      xrt_input_name name,
                                                      int64_t at_timestamp_ns,
                                                      xrt_space_relation * out_relation)
{
	*out_relation = static_cast<wivrn_xr_tracker *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
	return XRT_SUCCESS;
}

wivrn_xr_tracker::wivrn_xr_tracker(xrt_device * hmd, uint8_t id) :
        xrt_device{}, tracker_pose(id), tracker_id(id)
{
	xrt_device * base = this;
	base->tracking_origin = hmd->tracking_origin;

	base->update_inputs = wivrn_xr_tracker_update_inputs;
	base->get_tracked_pose = wivrn_xr_tracker_get_tracked_pose;
	base->destroy = wivrn_xr_tracker_destroy;
	name = XRT_DEVICE_VIVE_TRACKER;
	device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;
	orientation_tracking_supported = true;
	position_tracking_supported = true;

	// Print name.
	std::string tracker_name = "WiVRn Vive XR Tracker ";
	tracker_name.append(std::to_string(id));
	strcpy(str, tracker_name.c_str());
	strcpy(serial, tracker_name.c_str());

	tracker_input.active = true;
	tracker_input.name = XRT_INPUT_VIVE_TRACKER_GRIP_POSE;

	// Setup input.
	inputs = &tracker_input;
	input_count = 1;
}

void wivrn_xr_tracker::update_inputs()
{
	// Empty
}

xrt_space_relation wivrn_xr_tracker::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns)
{
	if (name == XRT_INPUT_VIVE_TRACKER_GRIP_POSE)
	{
		auto [_, relation] = tracker_pose.get_at(at_timestamp_ns);
		return relation;
	}

	std::cout << "Unknown input name" << std::endl;
	return {};
}

void wivrn_xr_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	tracker_pose.update_tracking(tracking, offset);
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

bool tracker_pose_list::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	for (const auto & tracker: tracking.motion_trackers)
	{
		if (tracker.id != device)
			continue;

		xrt_space_relation space{
		        .relation_flags = convert_flags(tracker.tracker_pose.flags),
		        .pose = xrt_cast(tracker.tracker_pose.pose),
		        .linear_velocity = xrt_cast(tracker.tracker_pose.linear_velocity),
		        .angular_velocity = xrt_cast(tracker.tracker_pose.angular_velocity)};

		return add_sample(tracking.production_timestamp, tracking.timestamp, space, offset);
	}
	return true;
}

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

		xrt::auxiliary::math::map_quat(res.pose.orientation) = xrt::auxiliary::math::map_quat(res.pose.orientation) * xrt::auxiliary::math::map_quat(dq);
	}

	return res;
}

/*
 *
 * Functions
 *
 */

static void wivrn_xr_tracker_destroy(xrt_device * xdev)
{
}

} // namespace wivrn
