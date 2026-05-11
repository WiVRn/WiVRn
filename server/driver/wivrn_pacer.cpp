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
#include <algorithm>
#include <cmath>

namespace wivrn
{

static const int64_t margin_ns = 3'000'000;
static const int64_t slop_ns = 500'000;

template <typename T>
static T lerp_mod(T a, T b, double t, T mod)
{
	if (2 * std::abs(a - b) < mod)
		return std::lerp(a, b, t);
	if (a < b)
		a += mod;
	else
		b += mod;
	return T(std::lerp(a, b, t)) % mod;
}

wivrn_pacer::wivrn_pacer(uint64_t frame_duration) :
        frame_duration_ns(frame_duration),
        frame_times(5000),
        frame_times_compute(frame_times),
        worker([this](std::stop_token t) {
	        std::vector<XrDuration> samples;
	        while (not t.stop_requested())
	        {
		        samples.clear();
		        {
			        std::unique_lock lock(compute_mutex);
			        compute_cv.wait(lock);
			        for (const auto & time: frame_times_compute)
			        {
				        if (time.decoded > time.present)
					        samples.push_back(time.decoded - time.present);
			        }
		        }
		        if (samples.empty())
			        continue;
		        auto it = samples.begin() + (samples.size() * 995) / 1000;
		        std::ranges::nth_element(samples, it);

		        std::unique_lock lock(mutex);
		        safe_present_to_decoded_ns = *it + 1'000'000;
	        }
        })
{}

wivrn_pacer::~wivrn_pacer()
{
	worker.request_stop();
	compute_cv.notify_all();
}

uint64_t wivrn_pacer::get_frame_duration()
{
	std::lock_guard lock(mutex);
	return frame_duration_ns;
}

void wivrn_pacer::set_frame_duration(uint64_t frame_duration_ns)
{
	std::lock_guard lock(mutex);
	this->frame_duration_ns = frame_duration_ns;
}

void wivrn_pacer::predict(
        int64_t & frame_id,
        int64_t & out_wake_up_time_ns,
        int64_t & out_desired_present_time_ns,
        int64_t & out_present_slop_ns,
        int64_t & out_predicted_display_time_ns)
{
	std::lock_guard lock(mutex);
	frame_id = this->frame_id++;
	auto now = os_monotonic_get_ns();

	int64_t predicted_client_render = last_ns + frame_duration_ns;
	// snap to phase
	predicted_client_render = (predicted_client_render / frame_duration_ns) * frame_duration_ns + client_render_phase_ns;

	if (now + mean_wake_up_to_present_ns + safe_present_to_decoded_ns > predicted_client_render)
		predicted_client_render += frame_duration_ns * ((now + mean_wake_up_to_present_ns + safe_present_to_decoded_ns - predicted_client_render) / frame_duration_ns);

	out_predicted_display_time_ns = predicted_client_render + mean_render_to_display_ns;
	out_desired_present_time_ns = predicted_client_render - safe_present_to_decoded_ns;
	out_wake_up_time_ns = out_desired_present_time_ns - mean_wake_up_to_present_ns + margin_ns; // we should be awoken early by the application
	last_wake_up_ns = out_wake_up_time_ns;

	last_ns = predicted_client_render;

	in_flight_frames[frame_id % in_flight_frames.size()] = {
	        .frame_id = frame_id,
	        .present_ns = out_desired_present_time_ns,
	        .predicted_display_time = out_predicted_display_time_ns,
	};

	out_present_slop_ns = slop_ns;
}

void wivrn_pacer::on_feedback(const wivrn::from_headset::feedback & feedback, const clock_offset & offset)
{
	if (feedback.times_displayed > 1 or not feedback.blitted)
		return;

	std::lock_guard lock(mutex);
	auto & when = in_flight_frames[feedback.frame_index % in_flight_frames.size()];
	if (when.frame_id != feedback.frame_index)
		return;

	auto & times = frame_times[feedback.frame_index % frame_times.size()];
	if (times.frame_id != feedback.frame_index)
	{
		times.frame_id = feedback.frame_index;
		times.present = when.present_ns;
		times.decoded = 0;
	}
	times.decoded = std::max(times.decoded, offset.from_headset(feedback.received_from_decoder));

	if (feedback.stream_index == 0)
	{
		if (feedback.frame_index % 100 == 0)
		{
			std::unique_lock lock(compute_mutex);
			std::swap(frame_times, frame_times_compute);
			compute_cv.notify_all();
		}

		client_render_phase_ns = lerp_mod<int64_t>(client_render_phase_ns, offset.from_headset(feedback.blitted) % frame_duration_ns, 0.1, frame_duration_ns);
	}

	if (feedback.displayed and feedback.displayed > feedback.blitted and feedback.displayed < feedback.blitted + 100'000'000)
		mean_render_to_display_ns = std::lerp(mean_render_to_display_ns, feedback.displayed - feedback.blitted, 0.1);
}
void wivrn_pacer::mark_timing_point(
        comp_target_timing_point point,
        int64_t frame_id,
        int64_t when_ns)
{
	switch (point)
	{
		//! Woke up after sleeping in wait frame.
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			return;

		//! Began CPU side work for GPU.
		case COMP_TARGET_TIMING_POINT_BEGIN:
			return;

		//! Just before submitting work to the GPU.
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			return;

		//! Just after submitting work to the GPU.
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			if (when_ns > last_wake_up_ns and when_ns < last_wake_up_ns + 100'000'000)
			{
				std::lock_guard lock(mutex);
				mean_wake_up_to_present_ns = std::lerp(mean_wake_up_to_present_ns, when_ns - last_wake_up_ns, 0.1);
			}
	}
}

wivrn_pacer::frame_info wivrn_pacer::present_to_info(int64_t present)
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
	std::ranges::fill(frame_times, frame_time{});
}
} // namespace wivrn
