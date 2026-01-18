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

class hand_joints_list : public history<hand_joints_list, xrt_hand_joint_set>
{
public:
	const int hand_id;

	static xrt_hand_joint_set interpolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, float t);
	static xrt_hand_joint_set extrapolate(const xrt_hand_joint_set & a, const xrt_hand_joint_set & b, int64_t ta, int64_t tb, int64_t t);

	hand_joints_list(int hand_id) :
	        hand_id(hand_id) {}

	void update_tracking(const wivrn::from_headset::hand_tracking & tracking, const clock_offset & offset);
};
} // namespace wivrn
