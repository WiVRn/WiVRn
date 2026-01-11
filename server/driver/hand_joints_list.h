/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"

namespace wivrn
{
struct clock_offset;

struct hand_aim_data
{
	bool valid = false;
	uint64_t status = 0;
	xrt_pose aim_pose{};
	float pinch_strength[4] = {}; // index, middle, ring, little
};

struct hand_tracking_data
{
	xrt_hand_joint_set joints{};
	hand_aim_data aim{};
};

class hand_joints_list : public history<hand_joints_list, hand_tracking_data>
{
public:
	const int hand_id;

	static hand_tracking_data interpolate(const hand_tracking_data & a, const hand_tracking_data & b, float t);
	static hand_tracking_data extrapolate(const hand_tracking_data & a, const hand_tracking_data & b, int64_t ta, int64_t tb, int64_t t);

	hand_joints_list(int hand_id) :
	        hand_id(hand_id) {}

	void update_tracking(const wivrn::from_headset::hand_tracking & tracking, const clock_offset & offset);
};
} // namespace wivrn
