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

#include "pose_list.h"
#include "wivrn_session.h"

struct tracked_views
{
	XrViewStateFlags flags;
	xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
	std::array<xrt_pose, 2> poses;
	std::array<xrt_fov, 2> fovs;
};

tracked_views interpolate(const tracked_views & a, const tracked_views & b, float t);

tracked_views extrapolate(const tracked_views & a, const tracked_views & b, uint64_t ta, uint64_t tb, uint64_t t);

class view_list : public history<tracked_views>
{
public:
	void update_tracking(const from_headset::tracking & tracking, const clock_offset & offset);
};
