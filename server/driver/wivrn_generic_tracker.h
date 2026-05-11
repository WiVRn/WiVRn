/*
 * WiVRn VR streaming
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

#pragma once

#include "history.h"
#include "xrt/xrt_device.h"

namespace wivrn
{
class wivrn_session;
struct clock_offset;

class tracker_pose_list : public history<tracker_pose_list, xrt_space_relation>
{
public:
	static xrt_space_relation interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t);
	static xrt_space_relation extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t);

	tracker_pose_list() = default;

	void update_tracking(XrTime produced_timestamp, XrTime timestamp, const from_headset::body_tracking::pose & pose, const clock_offset & offset);

	static xrt_space_relation convert_pose(const wivrn::from_headset::body_tracking::pose &);
};

class wivrn_generic_tracker : public xrt_device
{
	tracker_pose_list poses;
	xrt_input pose_input;

	wivrn_session & cnx;

public:
	using base = xrt_device;
	wivrn_generic_tracker(int index, xrt_device * hmd, wivrn_session & cnx);

	xrt_result_t update_inputs();
	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * out_relation);

	void update_tracking(const from_headset::body_tracking & tracking, const from_headset::body_tracking::pose & pose, const clock_offset & offset);
};
} // namespace wivrn
