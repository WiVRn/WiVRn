/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Awzri <awzri@awzricat.com>
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "xr/space.h"
#include <vector>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class vive_xr_tracker
{
	bool is_active = false;
	XrPath path;
	xr::space & space;

public:
	vive_xr_tracker(XrPath path, xr::space & s, XrSession session);
	void update_active(XrSession session);
	bool get_active() const;
	XrSpace get_space() const;
};

class htc_body_tracker
{
	XrSession s;
	std::vector<vive_xr_tracker> trackers;

public:
	using packet_type = wivrn::from_headset::htc_body;
	htc_body_tracker(session & s, std::vector<std::pair<XrPath, xr::space>> & trackers);

	void update_active();
	packet_type locate_spaces(XrTime time, XrSpace reference);
};
} // namespace xr
