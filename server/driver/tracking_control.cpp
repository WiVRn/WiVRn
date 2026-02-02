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

#include "tracking_control.h"

#include "driver/wivrn_connection.h"
#include "wivrn_config.h"
#include <magic_enum_containers.hpp>
#include <ranges>

bool wivrn::tracking_control::advance(std::chrono::steady_clock::time_point now)
{
	if (next > now)
		return false;
	next += step;
	return true;
}

void wivrn::tracking_control::add_request(device_id device, XrTime now, XrTime at_ns, XrTime produced_ns)
{
	std::lock_guard lock(mutex);
	XrDuration prediction = at_ns - now;
	reqs[device].min_prediction = std::min(reqs[device].min_prediction, prediction);
	reqs[device].max_prediction = std::max(reqs[device].max_prediction, prediction);

	if (produced_ns)
		motions_to_photons = std::max(motions_to_photons, at_ns - produced_ns);

	if (not std::ranges::contains(last_control.pattern, device, &to_headset::tracking_control::sample::device))
	{
		last_control.pattern.push_back({.device = device, .prediction_ns = prediction});
		cnx.send_control(to_headset::tracking_control(last_control));
	}
}

void wivrn::tracking_control::resolve(XrDuration frame_time, XrDuration latency)
{
	decltype(this->reqs) reqs;
	{
		std::lock_guard lock(mutex);
		std::swap(reqs, this->reqs);
	}

	wivrn::to_headset::tracking_control res{};

	for (auto [device, req]: std::ranges::enumerate_view(reqs))
	{
		// Skip unset data
		if (req.min_prediction > req.max_prediction)
			continue;

		XrDuration step = frame_time;
		switch (device_id(device))
		{
			// High frequency polling for those
			case device_id::HEAD:
			case device_id::LEFT_GRIP:
			case device_id::LEFT_AIM:
			case device_id::LEFT_PALM:
			case device_id::RIGHT_GRIP:
			case device_id::RIGHT_AIM:
			case device_id::RIGHT_PALM:
			case device_id::LEFT_PINCH_POSE:
			case device_id::RIGHT_PINCH_POSE:
			case device_id::EYE_GAZE:
				step = 3'000'000;
				break;
			case device_id::FACE:
				// Face tracking can't extrapolate
				res.pattern.push_back({
				        .device = device_id(device),
				        .prediction_ns = 0,
				});
				break;
			default:
				break;
		}

		req.min_prediction = std::clamp(req.min_prediction + latency, 0l, max_extrapolation_ns);
		req.max_prediction = std::clamp(req.max_prediction + latency, 0l, max_extrapolation_ns);

		for (int64_t t = req.min_prediction; t < req.max_prediction + step; t += step)
		{
			res.pattern.push_back({
			        .device = device_id(device),
			        .prediction_ns = t,
			});
		}
	}

	{
		std::lock_guard lock(mutex);
		res.motions_to_photons = motions_to_photons;
		last_control = res;
		motions_to_photons = 0;
	}
	cnx.send_control(std::move(res));
}
