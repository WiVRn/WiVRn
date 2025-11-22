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

#include "dkm/dkm.hpp"

#include "driver/wivrn_connection.h"
#include <magic_enum_containers.hpp>
#include <ranges>

void wivrn::tracking_control::add_request(device_id device, XrTime now, XrTime at_ns, XrTime produced_ns)
{
	std::lock_guard lock(mutex);
	samples.push_back(
	        {
	                .device = device,
	                .request_time = now,
	                .prediction = at_ns - now,
	                .motion_to_photons = produced_ns ? at_ns - produced_ns : 0,
	        });

	if (not std::ranges::contains(last_control.pattern, device, &to_headset::tracking_control::sample::device))
	{
		last_control.pattern.push_back({.device = device, .prediction_ns = at_ns - now});
		cnx.send_control(to_headset::tracking_control(last_control));
	}
}

/*
 * We have one sample each time the application or compositor requested some data.
 * Each sample stores when it was requested, for which device and how much in the future it was.
 *
 * We want to transform this into a pattern to be executed on headset so that it sends each item
 * just in time for it to be available on server.
 * In order to do this, we use k-means clustering with the time of the request (in range 0..frame_time)
 * and how much in the future the prediction was. We set k to the number of requests per frame.
 * As a result we get mean request and prediction time, which once sorted by request time is just
 * what we want.
 */
void wivrn::tracking_control::resolve(XrTime display_time, XrDuration frame_time, XrDuration latency)
{
	spare.clear();
	{
		std::lock_guard lock(mutex);
		std::swap(samples, spare);
	}

	wivrn::to_headset::tracking_control res{};

	if (spare.empty())
	{
		cnx.send_control(std::move(res));
		return;
	}

	using sample_t = std::vector<std::array<float, 1>>;

	magic_enum::containers::array<wivrn::device_id, sample_t> device_samples;

	const XrTime begin = spare.front().request_time;
	const XrTime end = spare.back().request_time;
	const float num_frames = std::max(1.f, float(end - begin) / frame_time);

	// Have a minimal margin
	latency += 1'000'000;

	size_t count = 0;

	// Group samples per device_id
	for (auto & item: spare)
	{
		device_samples[item.device].push_back(
		        {
		                float(item.prediction + latency),
		        });
		if (item.motion_to_photons)
		{
			++count;
			res.motions_to_photons += item.motion_to_photons;
		}
	}
	res.motions_to_photons /= count;

	std::vector<float> tmp;

	for (const auto & [device, samples]: std::ranges::enumerate_view(device_samples))
	{
		if (samples.empty())
			continue;

		int k = std::max<int>(1, std::round(samples.size() / num_frames));

		// Put samples into k clusters, and get the mean value of each cluster
		auto [means, group] = dkm::kmeans_lloyd(samples, k);

		// Merge until prediction times are sufficiently spaced apart
		for (; k > 1; --k)
		{
			std::ranges::sort(means);
			float prev = std::numeric_limits<float>::infinity();
			float diff = 1'000'000;
			for (const auto [prediction_ns]: means)
				diff = std::min(diff, prediction_ns - prev);
			if (diff == 1'000'000)
				break;
			std::tie(means, group) = dkm::kmeans_lloyd(samples, k);
		}

		// For each cluster, get the 90th percentile prediction time
		for (int i = 0; i < means.size(); ++i)
		{
			tmp.clear();
			for (const auto & [sample, group]: std::ranges::zip_view(samples, group))
			{
				if (group == i)
					tmp.push_back(std::get<0>(sample));
			}
			auto it = tmp.begin() + (tmp.size() * 9) / 10;
			std::ranges::nth_element(tmp, it);

			res.pattern.push_back({
			        .device = wivrn::device_id(device),
			        .prediction_ns = XrDuration(*it),
			});
		}
	}

	{
		std::lock_guard lock(mutex);
		last_control = res;
	}
	cnx.send_control(std::move(res));
}
