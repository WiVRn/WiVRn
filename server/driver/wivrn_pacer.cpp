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

void wivrn_pacer::set_stream_count(size_t count)
{
	std::lock_guard lock(mutex);
	last_feedback.resize(count);
}

void wivrn_pacer::predict(
        uint64_t & out_wake_up_time_ns,
        uint64_t & out_desired_present_time_ns,
        uint64_t & out_present_slop_ns,
        uint64_t & out_predicted_display_time_ns)
{
	std::lock_guard lock(mutex);
	if (next_frame_ns == 0)
		next_frame_ns = os_monotonic_get_ns();

	next_frame_ns += frame_duration_ns;

	out_wake_up_time_ns = next_frame_ns;
	out_desired_present_time_ns = out_wake_up_time_ns + mean_wake_up_to_present_ns;
	out_present_slop_ns = 0;
	out_predicted_display_time_ns = out_desired_present_time_ns;
}

void wivrn_pacer::on_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback, const clock_offset& offset)
{
	std::lock_guard lock(mutex);
	if (feedback.stream_index >= last_feedback.size())
		return;

	auto & last = last_feedback[feedback.stream_index];

	if (feedback.stream_index == 0 and
	    last.displayed and
	    feedback.displayed and
	    feedback.frame_index == last.frame_index + 1)
	{
		frame_duration_ns = std::lerp(frame_duration_ns, (feedback.displayed - last.displayed) / offset.a, 0.1);
	}

	last = feedback;

	bool same_frame = true;
	uint64_t decoded_ns = 0;
	for (const auto & f: last_feedback)
	{
		same_frame = same_frame and (f.frame_index == feedback.frame_index);
		decoded_ns = std::max(decoded_ns, f.received_from_decoder);
	}
	if (same_frame and feedback.blitted)
	{
		mean_client_wait_ns = std::lerp(mean_client_wait_ns, feedback.blitted - decoded_ns, 0.1);
	}

	if (mean_client_wait_ns > frame_duration_ns / 2)
		next_frame_ns += frame_duration_ns / 1000;
	if (mean_client_wait_ns < frame_duration_ns / 4)
		next_frame_ns -= frame_duration_ns / 1000;
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
