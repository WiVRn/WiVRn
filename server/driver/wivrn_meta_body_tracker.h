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

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "history.h"
#include "pose_list.h"
#include "wivrn_packets.h"

namespace wivrn
{
class wivrn_session;
struct clock_offset;

class meta_body_joints_list : public history<meta_body_joints_list, xrt_full_body_joint_set_meta>
{
public:
	static xrt_full_body_joint_set_meta interpolate(const xrt_full_body_joint_set_meta & a, const xrt_full_body_joint_set_meta & b, float t);
	static xrt_full_body_joint_set_meta extrapolate(const xrt_full_body_joint_set_meta & a, const xrt_full_body_joint_set_meta & b, int64_t ta, int64_t tb, int64_t t);

	void update_tracking(const wivrn::from_headset::meta_body & tracking, const clock_offset & offset);
};

class wivrn_meta_body_tracker : public xrt_device
{
	meta_body_joints_list joints_list;
	std::array<xrt_input, 2> inputs_array;

	wivrn_session & cnx;

public:
	using base = xrt_device;
	wivrn_meta_body_tracker(xrt_device * hmd, wivrn_session & cnx);

	xrt_result_t update_inputs();
	xrt_result_t get_body_skeleton(xrt_input_name body_tracking_type, xrt_body_skeleton * out_value);
	xrt_result_t get_body_joints(xrt_input_name body_tracking_type, int64_t at_timestamp_ns, xrt_body_joint_set * out_value);

	void update_tracking(const from_headset::meta_body &, const clock_offset &);
	void update_skeleton(const from_headset::meta_body_skeleton &);
};

} // namespace wivrn