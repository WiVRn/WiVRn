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

#pragma once

#include "xrt/xrt_device.h"

#include "hand_joints_list.h"
#include "pose_list.h"

#include <mutex>
#include <vector>

namespace wivrn
{
class wivrn_session;

class wivrn_hand_interaction : public xrt_device
{
	std::mutex mutex;

	pose_list grip;
	pose_list aim;
	pose_list palm;
	pose_list pinch_ext;
	pose_list poke_ext;

	std::vector<xrt_input> inputs_staging;
	std::vector<xrt_input> inputs_array;

	wivrn::wivrn_session * cnx;

public:
	using base = xrt_device;
	wivrn_hand_interaction(int hand_id, xrt_device * hmd, wivrn::wivrn_session * cnx);

	xrt_result_t update_inputs();

	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * out_relation);

	void set_inputs(const from_headset::inputs &, const clock_offset &);

	void set_derived_pose(const from_headset::derived_pose &);
	void update_tracking(const from_headset::tracking &, const clock_offset &);

	void reset_history();
};
} // namespace wivrn
