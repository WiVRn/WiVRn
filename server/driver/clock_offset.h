/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace wivrn
{

class wivrn_connection;

struct clock_offset
{
	// y: headset time
	// x: server time
	// y = x+b
	int64_t b = 0;
	bool stable = false;

	operator bool() const
	{
		return stable;
	}

	XrTime from_headset(XrTime) const;

	XrTime to_headset(XrTime timestamp_ns) const;
};

class clock_offset_estimator
{
	struct sample : public wivrn::from_headset::timesync_response
	{
		XrTime received;
	};

	std::mutex mutex;
	std::vector<sample> samples;
	size_t sample_index = 0;
	std::atomic<int64_t> b = 0; // lest significant bit == stable

	std::chrono::steady_clock::time_point next_sample{};
	std::atomic<std::chrono::milliseconds> sample_interval = std::chrono::milliseconds(10);

public:
	std::chrono::steady_clock::time_point next() const;
	void reset();
	void request_sample(std::chrono::steady_clock::time_point now, wivrn_connection & connection);
	void add_sample(const wivrn::from_headset::timesync_response & sample);

	clock_offset get_offset();
};
} // namespace wivrn
