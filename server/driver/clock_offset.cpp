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

#include "clock_offset.h"

#include "driver/wivrn_connection.h"
#include "os/os_time.h"
#include "util/u_logging.h"

namespace wivrn
{

static const size_t num_samples = 100;

std::chrono::steady_clock::time_point clock_offset_estimator::next() const
{
	return next_sample;
}

void clock_offset_estimator::reset()
{
	std::lock_guard lock(mutex);
	sample_index = 0;
	samples.clear();
	b = 0;
	next_sample = {};
	sample_interval = std::chrono::milliseconds(10);
}

void clock_offset_estimator::request_sample(std::chrono::steady_clock::time_point now, wivrn_connection & connection)
{
	if (now < next_sample)
		return;

	next_sample = now + sample_interval.load();
	connection.send_stream(
	        wivrn::to_headset::timesync_query{
	                .query = XrTime(os_monotonic_get_ns()),
	        });
}

void clock_offset_estimator::add_sample(const wivrn::from_headset::timesync_response & base_sample)
{
	XrTime now = os_monotonic_get_ns();
	clock_offset_estimator::sample sample{base_sample, now};
	std::lock_guard lock(mutex);
	if (samples.size() < num_samples)
	{
		samples.push_back(sample);
	}
	else
	{
		sample_interval = std::chrono::milliseconds(100);
		int64_t latency = 0;
		for (const auto & s: samples)
			latency += s.received - s.query;
		latency /= samples.size();
		// packets with too high latency are likely to be retransmitted
		if (sample.received - sample.query > 3 * latency)
		{
			U_LOG_D("drop packet for latency %" PRIi64 "µs > %" PRIi64 "µs", (sample.received - sample.query) / 1000, latency / 1000);
			return;
		}

		samples[sample_index] = sample;
		sample_index = (sample_index + 1) % num_samples;
	}

	// Linear regression
	// X = time on server
	// Y = time on headset
	// in order to maintain accuracy, use x = X-x0, y = Y-y0
	// where x0 and y0 are means of X an Y
	size_t n = samples.size();
	double inv_n = 1. / n;
	double x0 = 0;
	double y0 = 0;
	for (const auto & s: samples)
	{
		x0 += (s.query + s.received) * 0.5;
		y0 += s.response;
	}
	x0 *= inv_n;
	y0 *= inv_n;

	if (samples.size() < num_samples)
	{
		int64_t b = y0 - x0;
		b &= ~int64_t(1);
		this->b = b;
		return;
	}

	double sum_x = 0;
	double sum_y = 0;
	for (const auto & s: samples)
	{
#if 1
		// assume symmetrical latency
		double x = (s.query + s.received) * 0.5 - x0;
#else
		// assume latency is only on server -> headset link
		double x = s.received - x0;
#endif
		double y = s.response - y0;
		sum_x += x;
		sum_y += y;
	}

	double mean_x = sum_x * inv_n;
	double mean_y = sum_y * inv_n;

	double b = y0 + (mean_y - mean_x) - x0;

	// b changed less than 20ms
	bool stable = std::abs((b - (double)this->b)) < 20'000'000;

	int64_t new_b = b;
	if (stable)
		new_b |= 1;
	else
		new_b &= ~int64_t(1);
	this->b = new_b;
	U_LOG_T("clock relations: headset = x+b where b=%" PRIu64 "µs", new_b / 1000);
}

clock_offset clock_offset_estimator::get_offset()
{
	auto b = this->b.load();
	return clock_offset{
	        .b = b,
	        .stable = bool(b & 1),
	};
}

XrTime clock_offset::from_headset(XrTime ts) const
{
	return ts - b;
}

XrTime clock_offset::to_headset(XrTime timestamp_ns) const
{
	return timestamp_ns + b;
}
} // namespace wivrn
