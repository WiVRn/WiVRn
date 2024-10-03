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

#pragma once

#include "xrt/xrt_device.h"

#include "hand_joints_list.h"
#include "pose_list.h"
#include "wivrn_connection.h"

#include <memory>
#include <mutex>
#include <vector>

namespace xrt::drivers::wivrn
{
class wivrn_session;
}

class wivrn_controller : public xrt_device
{
	std::mutex mutex;

	pose_list grip;
	pose_list aim;
	hand_joints_list joints;

	std::vector<xrt_input> inputs_staging;
	std::vector<xrt_input> inputs_array;
	xrt_output haptic_output;

	xrt::drivers::wivrn::wivrn_session * cnx;

public:
	wivrn_controller(int hand_id, xrt_device * hmd, xrt::drivers::wivrn::wivrn_session * cnx);

	void unregister()
	{
		cnx = nullptr;
	}
	void update_inputs();

	xrt_space_relation get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns);
	std::pair<xrt_hand_joint_set, int64_t> get_hand_tracking(xrt_input_name name, int64_t desired_timestamp_ns);

	void set_output(xrt_output_name name, const xrt_output_value * value);

	void set_inputs(const from_headset::inputs &, const clock_offset &);

	void update_tracking(const from_headset::tracking &, const clock_offset &);
	void update_hand_tracking(const from_headset::hand_tracking &, const clock_offset &);

private:
	void set_inputs(device_id input_id, float value, int64_t last_change_time);
};
