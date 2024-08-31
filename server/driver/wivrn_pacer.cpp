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

#include "wivrn_pacer.h"
#include "driver/clock_offset.h"
#include "os/os_time.h"
#include <cmath>

// How many samples of wait time to store per decoder
const size_t num_wait_times = 100;
// How many samples of wait time are required to use them
const size_t min_wait_times = 50;

void wivrn_pacer::set_stream_count(size_t count)
{
	std::lock_guard lock(mutex);
	streams.resize(count);
	for (auto & stream: streams)
		stream.times.reserve(num_wait_times);
}

void wivrn_pacer::predict(
        int64_t & frame_id,
        uint64_t & out_wake_up_time_ns,
        uint64_t & out_desired_present_time_ns,
        uint64_t & out_present_slop_ns,
        uint64_t & out_predicted_display_time_ns)
{
	std::lock_guard lock(mutex);
	frame_id = this->frame_id++;
	auto now = os_monotonic_get_ns();

	uint64_t predicted_client_render = last_ns + frame_duration_ns;
	// snap to phase
	predicted_client_render = (predicted_client_render / frame_duration_ns) * frame_duration_ns + client_render_phase_ns;

	if (now + mean_wake_up_to_present_ns + safe_present_to_decoded_ns > predicted_client_render)
		predicted_client_render += frame_duration_ns * ((now + mean_wake_up_to_present_ns + safe_present_to_decoded_ns - predicted_client_render) / frame_duration_ns);

	out_predicted_display_time_ns = predicted_client_render + mean_render_to_display_ns;
	out_desired_present_time_ns = predicted_client_render - safe_present_to_decoded_ns;
	out_wake_up_time_ns = out_desired_present_time_ns - mean_wake_up_to_present_ns;

	last_ns = predicted_client_render;

	in_flight_frames[frame_id % in_flight_frames.size()] = {
	        .frame_id = frame_id,
	        .present_ns = out_desired_present_time_ns,
	        .predicted_display_time = out_predicted_display_time_ns,
	};

	out_present_slop_ns = 0;
}

void wivrn_pacer::on_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback, const clock_offset & offset)
{
	std::lock_guard lock(mutex);
	if (feedback.stream_index >= streams.size() or feedback.times_displayed > 1)
		return;

	auto & when = in_flight_frames[feedback.frame_index % in_flight_frames.size()];
	if (when.frame_id != feedback.frame_index)
		return;

	auto & stream = streams[feedback.stream_index];
	if (stream.times.size() < num_wait_times)
		stream.times.push_back(offset.from_headset(feedback.received_from_decoder) - when.present_ns);
	else
	{
		stream.times[stream.next_times_index] = offset.from_headset(feedback.received_from_decoder) - when.present_ns;
		stream.next_times_index = (stream.next_times_index + 1) % num_wait_times;
	}

	if (feedback.stream_index == 0)
	{
		uint64_t safe_time = 0;
		for (const auto & stream: streams)
		{
			if (stream.times.size() >= min_wait_times)
			{
				double mean_time = 0;
				double time_variance = 0;

				// https://en.wikipedia.org/wiki/Standard_deviation#Rapid_calculation_methods
				for (size_t i = 0; i < stream.times.size(); ++i)
				{
					int64_t t = stream.times[i];
					double prev_mean = mean_time;
					mean_time += (t - mean_time) / (i + 1);
					time_variance += (t - prev_mean) * (t - mean_time);
				}

				time_variance /= (stream.times.size() - 1);
				double time_std_dev = std::sqrt(time_variance);

				safe_time = std::max<uint64_t>(safe_time, mean_time + 3 * time_std_dev);
			}
		}
		if (safe_time > 0 and safe_time < 100'000'000)
			safe_present_to_decoded_ns = std::lerp(safe_present_to_decoded_ns, safe_time, 0.1);

		client_render_phase_ns = std::lerp(client_render_phase_ns, offset.from_headset(feedback.blitted) % frame_duration_ns, 0.1);
	}

	if (feedback.displayed)
		mean_render_to_display_ns = std::lerp(mean_render_to_display_ns, feedback.displayed - feedback.blitted, 0.1);
}
void wivrn_pacer::mark_timing_point(
        comp_target_timing_point point,
        int64_t frame_id,
        uint64_t when_ns)
{
	std::lock_guard lock(mutex);
	switch (point)
	{
		//! Woke up after sleeping in wait frame.
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			last_wake_up_ns = when_ns;
			return;

		//! Began CPU side work for GPU.
		case COMP_TARGET_TIMING_POINT_BEGIN:
			return;

		//! Just before submitting work to the GPU.
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			return;

		//! Just after submitting work to the GPU.
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			mean_wake_up_to_present_ns = std::lerp(mean_wake_up_to_present_ns, when_ns - last_wake_up_ns, 0.1);
	}
}

wivrn_pacer::frame_info wivrn_pacer::present_to_info(uint64_t present)
{
	std::lock_guard lock(mutex);
	for (const auto & info: in_flight_frames)
	{
		if (info.present_ns == present)
			return info;
	}
	assert(false);
	return {};
}

void wivrn_pacer::reset()
{
	std::lock_guard lock(mutex);
	for (auto & stream: streams)
	{
		stream.times.clear();
		stream.next_times_index = 0;
	}
	in_flight_frames = {};
}
