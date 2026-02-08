/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include "utils/thread_safe.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "history.h"
#include "pose_list.h"
#include "wivrn_packets.h"

#include <atomic>

namespace wivrn
{
class wivrn_session;
struct clock_offset;

class body_joints_list : public history<body_joints_list, xrt_body_joint_set>
{
	wivrn::from_headset::body_type type;

public:
	body_joints_list(wivrn::from_headset::body_type type) : type(type) {}
	xrt_body_joint_set interpolate(const xrt_body_joint_set & a, const xrt_body_joint_set & b, float t);
	xrt_body_joint_set extrapolate(const xrt_body_joint_set & a, const xrt_body_joint_set & b, int64_t ta, int64_t tb, int64_t t);

	void update_tracking(const wivrn::from_headset::meta_body & tracking, const clock_offset & offset);
	void update_tracking(const wivrn::from_headset::bd_body & tracking, const clock_offset & offset);
};

class wivrn_body_tracker : public xrt_device
{
	body_joints_list joints_list;
	std::vector<xrt_input> inputs_array;

	wivrn_session & cnx;

	thread_safe<xrt_body_skeleton> meta_skeleton;
	std::atomic_uint32_t meta_skeleton_generation;

public:
	using base = xrt_device;
	wivrn_body_tracker(xrt_device * hmd, wivrn_session & cnx);

	xrt_result_t update_inputs();
	xrt_result_t get_body_skeleton(xrt_input_name body_tracking_type, xrt_body_skeleton * out_value);
	xrt_result_t get_body_joints(xrt_input_name body_tracking_type, int64_t at_timestamp_ns, xrt_body_joint_set * out_value);

	void update_tracking(const from_headset::meta_body &, const clock_offset &);
	void update_tracking(const from_headset::bd_body &, const clock_offset &);
	void update_skeleton(const from_headset::meta_body_skeleton &);
};

} // namespace wivrn