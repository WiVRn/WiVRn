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

#include <array>
#include <openxr/openxr.h>

namespace wivrn
{

struct tracked_views
{
	XrViewStateFlags flags;
	xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
	std::array<xrt_pose, 2> poses;
	std::array<xrt_fov, 2> fovs;
};

class view_list
{
	pose_list head_poses{device_id::HEAD};
	std::mutex mutex;
	XrViewStateFlags flags;
	std::array<xrt_pose, 2> poses;
	std::array<xrt_fov, 2> fovs;

public:
	void update_tracking(const from_headset::tracking & tracking, const clock_offset & offset);
	std::pair<XrTime, tracked_views> get_at(XrTime at_timestamp_ns);
};
} // namespace wivrn
