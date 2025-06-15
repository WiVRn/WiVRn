/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Awzri <awzri@awzricat.com>
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

#include "wivrn_packets.h"
#include "xr/htc_exts.h"
#include "xr/space.h"
#include <string>
#include <vector>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class vive_xr_tracker
{
	bool is_active = false;
	xr::space space;

public:
	vive_xr_tracker(xr::space && s);
	void set_active(bool active);
	bool get_active() const;
	XrSpace get_space() const;
};

class htc_body_tracker
{
	instance * inst;
	XrSession s;
	std::vector<vive_xr_tracker> trackers;

	PFN_xrEnumeratePathsForInteractionProfileHTC xrEnumeratePathsForInteractionProfileHTC{};

	std::vector<std::string> get_roles();

public:
	htc_body_tracker() = default;
	htc_body_tracker(instance & inst, session & s);

	std::vector<XrPath> get_paths(XrPath user_path = XR_NULL_PATH);
	void add(xr::space && space);
	size_t count() const;
	void update_active();
	std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses> locate_spaces(XrTime time, XrSpace reference);
};
} // namespace xr
