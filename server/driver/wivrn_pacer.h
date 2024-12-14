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

#pragma once

#include "wivrn_packets.h"

#include <cstdint>
#include <main/comp_target.h>
#include <mutex>
#include <vector>

namespace wivrn
{

struct clock_offset;

class wivrn_pacer
{
public:
	struct frame_info
	{
		int64_t frame_id;
		int64_t present_ns;
		int64_t predicted_display_time;
	};

private:
	std::mutex mutex;
	uint64_t frame_duration_ns;
	int64_t last_ns = 0;
	int64_t frame_id = 0;

	int64_t client_render_phase_ns = 0;

	int64_t mean_wake_up_to_present_ns = 1'000'000;
	int64_t safe_present_to_decoded_ns = 0;
	int64_t mean_render_to_display_ns = 0;

	int64_t last_wake_up_ns = 0;

	struct frame_time
	{
		int64_t frame_id = -1;
		XrTime present = 0;
		XrTime decoded = 0;
	};
	std::vector<frame_time> frame_times;

	std::array<frame_info, 4> in_flight_frames;

public:
	wivrn_pacer(uint64_t frame_duration) :
	        frame_duration_ns(frame_duration),
	        frame_times(5000)
	{}

	void set_frame_duration(uint64_t frame_duration);

	void predict(
	        int64_t & out_frame_id,
	        int64_t & out_wake_up_time_ns,
	        int64_t & out_desired_present_time_ns,
	        int64_t & out_present_slop_ns,
	        int64_t & out_predicted_display_time_ns);

	void on_feedback(const wivrn::from_headset::feedback &, const clock_offset &);

	void mark_timing_point(
	        comp_target_timing_point point,
	        int64_t frame_id,
	        int64_t when_ns);

	frame_info present_to_info(int64_t present);

	void reset();
};
} // namespace wivrn
