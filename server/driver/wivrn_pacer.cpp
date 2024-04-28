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
        uint64_t & out_wake_up_time_ns,
        uint64_t & out_desired_present_time_ns,
        uint64_t & out_present_slop_ns,
        uint64_t & out_predicted_display_time_ns)
{
	std::lock_guard lock(mutex);
	auto now = os_monotonic_get_ns();

	if (next_frame_ns == 0)
		next_frame_ns = now;

	next_frame_ns += frame_duration_ns;

	if (next_frame_ns < now)
		next_frame_ns = now;

	out_wake_up_time_ns = next_frame_ns;
	out_desired_present_time_ns = out_wake_up_time_ns + mean_wake_up_to_present_ns;
	out_present_slop_ns = 0;
	out_predicted_display_time_ns = out_desired_present_time_ns + mean_present_to_display_ns;
}

void wivrn_pacer::on_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback, const clock_offset & offset)
{
	std::lock_guard lock(mutex);
	if (feedback.stream_index >= streams.size())
		return;

	auto & last = streams[feedback.stream_index].last_feedback;

	if (feedback.stream_index == 0 and
	    last.displayed and
	    feedback.displayed and
	    feedback.frame_index == last.frame_index + 1)
	{
		frame_duration_ns = std::lerp(frame_duration_ns, (feedback.displayed - last.displayed) / offset.a, 0.1);
	}
	last = feedback;

	auto blitted = feedback.blitted;
	if (last.blitted and feedback.received_from_decoder and not feedback.blitted)
	{
		// We skipped a frame, certainly because it was too late
		// estimate the moment it should have been displayed
		blitted = last.blitted + frame_duration_ns * (int64_t(feedback.frame_index) - int64_t(last.frame_index));
	}

	if (blitted and feedback.times_displayed <= 1)
	{
		auto & stream = streams[feedback.stream_index];
		if (stream.times.size() < num_wait_times)
			stream.times.push_back(blitted - feedback.received_from_decoder);
		else
		{
			stream.times[stream.next_times_index] = blitted - feedback.received_from_decoder;
			stream.next_times_index = (stream.next_times_index + 1) % num_wait_times;
		}

		if (feedback.stream_index == 0)
		{
			bool wait_more = false;
			bool wait_less = true;
			for (const auto & stream: streams)
			{
				if (stream.times.size() >= min_wait_times)
				{
					double mean_wait_time = 0;
					double wait_time_variance = 0;

					// https://en.wikipedia.org/wiki/Standard_deviation#Rapid_calculation_methods
					for (size_t i = 0; i < stream.times.size(); ++i)
					{
						int64_t t = stream.times[i];
						double prev_mean = mean_wait_time;
						mean_wait_time += (t - mean_wait_time) / (i + 1);
						wait_time_variance += (t - prev_mean) * (t - mean_wait_time);
					}

					wait_time_variance /= (stream.times.size() - 1);
					double wait_time_std_dev = std::sqrt(wait_time_variance);

					if (mean_wait_time < 4 * wait_time_std_dev)
						wait_less = false;
					if (mean_wait_time < 3 * wait_time_std_dev)
						wait_more = true;
				}
			}
			if (wait_more)
				next_frame_ns -= frame_duration_ns / 1000;
			else if (wait_less)
				next_frame_ns += frame_duration_ns / 1000;
		}
	}
	if (feedback.displayed)
	{
		auto & when = in_flight_frames[feedback.frame_index % in_flight_frames.size()];
		if (when.frame_id == feedback.frame_index)
		{
			mean_present_to_display_ns = std::lerp(mean_present_to_display_ns, offset.from_headset(feedback.displayed) - when.present_ns, 0.1);
			when.frame_id = 0;
		}
	}
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
			in_flight_frames[frame_id % in_flight_frames.size()] = {.frame_id = uint64_t(frame_id), .present_ns = when_ns};
	}
}
