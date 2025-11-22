/*
 * WiVRn VR streaming
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
#include <mutex>

namespace wivrn
{
class wivrn_connection;
class tracking_control
{
	struct sample
	{
		device_id device;
		XrTime request_time;
		XrDuration prediction;
		XrDuration motion_to_photons;
	};

	std::mutex mutex;
	std::vector<sample> samples;
	std::vector<sample> spare;
	to_headset::tracking_control last_control;

	wivrn_connection & cnx;

public:
	tracking_control(wivrn_connection & cnx) : cnx(cnx) {}
	void add_request(device_id device, int64_t now, int64_t at_ns, int64_t produced_ns);

	void resolve(XrTime display_time, XrDuration frame_time, XrDuration latency);
};
} // namespace wivrn
