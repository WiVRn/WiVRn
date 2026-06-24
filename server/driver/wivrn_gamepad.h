/*
 * WiVRn VR streaming
 * Copyright (C) 2026  JR Lanteigne <root@dnim.dev>
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

#include "wivrn_packets.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include <array>
#include <mutex>

namespace wivrn
{
class wivrn_session;

// A gamepad forwarded from the headset, exposed as an OpenXR xbox_controller (/user/gamepad).
// Inputs stay inactive until the headset reports a gamepad is connected.
class wivrn_gamepad : public xrt_device
{
	std::mutex mutex;
	bool connected = false;

	xrt_tracking_origin origin;
	std::array<xrt_input, 18> inputs_array;
	std::array<xrt_input, 18> inputs_staging; // written from the network thread, published by update_inputs
	std::array<xrt_output, 4> outputs_array;

	wivrn::wivrn_session * cnx;

public:
	using base_t = xrt_device;
	wivrn_gamepad(wivrn::wivrn_session & cnx);

	xrt_result_t update_inputs();
	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * out_relation);
	xrt_result_t set_output(xrt_output_name name, const xrt_output_value * value);

	// Update from a forwarded inputs packet (gamepad device_ids).
	void set_inputs(const from_headset::inputs &);
	void set_connected(bool);
};

} // namespace wivrn
