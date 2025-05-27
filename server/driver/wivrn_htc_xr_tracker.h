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

#include "driver/clock_offset.h"
#include "driver/history.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include <cstdint>
#include <mutex>

namespace wivrn
{

class tracker_pose_list : public history<tracker_pose_list, xrt_space_relation>
{
    std::atomic<tracker_pose_list *> source = nullptr;
	xrt_pose offset;
	std::atomic_bool derive_forced = false;
public:
    const uint8_t device;
    static xrt_space_relation extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t);
    static xrt_space_relation interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t);

    tracker_pose_list(uint8_t id) :
            device(id) {}

    bool update_tracking(const from_headset::tracking & tracking, const clock_offset & offset);
};

class wivrn_xr_tracker : public xrt_device
{
	std::mutex mutex;
	xrt_input tracker_input;
	tracker_pose_list tracker_pose;

public:
    const uint8_t tracker_id;
	wivrn_xr_tracker(xrt_device * hmd, uint8_t id);

	void update_inputs();
	void update_tracking(const from_headset::tracking &, const clock_offset &);
	xrt_space_relation get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns);
};
} // namespace wivrn
