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

#pragma once

#include "pose_list.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include <cstdint>
#include <mutex>

namespace wivrn
{
class wivrn_session;

class wivrn_eye_tracker : public xrt_device
{
	std::mutex mutex;
	xrt_input gaze_input;
	pose_list gaze;
	wivrn_session & cnx;

public:
	using base = xrt_device;
	wivrn_eye_tracker(xrt_device * hmd, wivrn_session &);

	xrt_result_t update_inputs();
	void update_tracking(const from_headset::tracking &, const clock_offset &);

	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * out_relation);
};
} // namespace wivrn
