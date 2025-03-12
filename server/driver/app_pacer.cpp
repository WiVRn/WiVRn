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

#include "app_pacer.h"

#include "util/u_time.h"
#include "utils/method.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <ranges>

namespace wivrn
{

class app_pacer : public u_pacing_app
{
	pacing_app_factory & parent;
	int64_t frame_id = 0;
	int64_t compositor_display_time = 0;
	int64_t last_display_time = 0;
	int64_t period = 10'000'000;

	std::mutex mutex; // locks cpu/draw/gpu time
	int64_t cpu_time = 0;
	int64_t gpu_time = 0;
	int64_t compositor_time = 0;

	struct frame
	{
		int64_t frame_id;
		int64_t wake_up;
		int64_t delivered;
	};
	std::array<frame, 16> frames{};

	frame & get_frame(int64_t id)
	{
		assert(id >= 0);
		return frames[id % frames.size()];
	}

public:
	using base = u_pacing_app;

	app_pacer(pacing_app_factory & parent) :
	        u_pacing_app{
	                .predict = method_pointer<&app_pacer::predict>,
	                .mark_point = method_pointer<&app_pacer::mark_point>,
	                .mark_discarded = method_pointer<&app_pacer::mark_discarded>,
	                .mark_delivered = method_pointer<&app_pacer::mark_delivered>,
	                .mark_gpu_done = method_pointer<&app_pacer::mark_gpu_done>,
	                .latched = method_pointer<&app_pacer::latched>,
	                .retired = method_pointer<&app_pacer::retired>,
	                .info = method_pointer<&app_pacer::info>,
	                .destroy = method_pointer<&app_pacer::destroy>,
	        },
	        parent(parent)
	{}

	void predict(int64_t now_ns,
	             int64_t * out_frame_id,
	             int64_t * out_wake_up_time,
	             int64_t * out_predicted_display_time,
	             int64_t * out_predicted_display_period);
	void mark_point(int64_t frame_id, enum u_timing_point point, int64_t when_ns);
	void mark_discarded(int64_t frame_id, int64_t when_ns);
	void mark_delivered(int64_t frame_id, int64_t when_ns, int64_t display_time_ns);
	void mark_gpu_done(int64_t frame_id, int64_t when_ns);
	void latched(int64_t frame_id, int64_t when_ns, int64_t system_frame_id) {}
	void retired(int64_t frame_id, int64_t when_ns) {}
	void info(int64_t predicted_display_time_ns,
	          int64_t predicted_display_period_ns,
	          int64_t extra_ns);

	int64_t get_app_time()
	{
		std::lock_guard lock(mutex);
		return std::max(cpu_time, gpu_time);
	}

	void destroy()
	{
		parent.remove_app(this);
		delete this;
	}
};

void app_pacer::predict(int64_t now_ns,
                        int64_t * out_frame_id,
                        int64_t * out_wake_up_time,
                        int64_t * out_predicted_display_time,
                        int64_t * out_predicted_display_period)
{
	*out_frame_id = ++frame_id;
	get_frame(frame_id) = {.frame_id = frame_id};

	// no need to lock, called in same thread as writes
	auto min_ready = now_ns + cpu_time + gpu_time + compositor_time;
	// The ideal display time: one frame after the last
	last_display_time += period;
	// Sync phase with compositor
	last_display_time = compositor_display_time + period * ((period / 2 + last_display_time - compositor_display_time) / period);

	if (cpu_time > period or gpu_time > period or (min_ready > last_display_time and min_ready < last_display_time + period))
	{
		// We are limited by app time, don't wait
		*out_wake_up_time = now_ns;
		*out_predicted_display_period = period; // FIXME: may be more than one frame
		while (last_display_time < min_ready)
			last_display_time += period;
		*out_predicted_display_time = last_display_time;
	}
	else
	{
		// Ensure display time is achievable
		while (last_display_time < min_ready)
			last_display_time += period;

		*out_predicted_display_time = last_display_time;
		*out_predicted_display_period = period;
		*out_wake_up_time = last_display_time - (cpu_time + gpu_time + compositor_time + U_TIME_1MS_IN_NS);
	}
}

void app_pacer::mark_point(int64_t frame_id, enum u_timing_point point, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	switch (point)
	{
		case U_TIMING_POINT_WAKE_UP:
			frame.wake_up = when_ns;
			break;
		case U_TIMING_POINT_BEGIN:
		case U_TIMING_POINT_SUBMIT_BEGIN:
		case U_TIMING_POINT_SUBMIT_END:
			break;
	}
}

void app_pacer::mark_delivered(int64_t frame_id, int64_t when_ns, int64_t display_time_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	frame.delivered = when_ns;
}

void app_pacer::mark_discarded(int64_t frame_id, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	frame.delivered = when_ns;
}

void app_pacer::info(int64_t predicted_display_time_ns,
                     int64_t predicted_display_period_ns,
                     int64_t extra_ns)
{
	compositor_display_time = predicted_display_time_ns;
	period = predicted_display_period_ns;
	compositor_time = std::max<int64_t>(0, extra_ns);
}

int64_t lerp0(int64_t a, int64_t b, double t)
{
	if (a == 0)
		return b;
	return std::lerp(a, b, t);
}

void app_pacer::mark_gpu_done(int64_t frame_id, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id or not(frame.wake_up and frame.delivered))
		return;

	std::lock_guard lock(mutex);
	cpu_time = lerp0(cpu_time, frame.delivered - frame.wake_up, 0.1);
	gpu_time = lerp0(gpu_time, when_ns - frame.delivered, 0.1);
}

pacing_app_factory::pacing_app_factory() :
        u_pacing_app_factory{
                .create = method_pointer<&pacing_app_factory::create>,
                .destroy = method_pointer<&pacing_app_factory::destroy>,
        }
{
}

void pacing_app_factory::remove_app(app_pacer * app)
{
	std::lock_guard lock(mutex);
	std::erase(app_pacers, app);
}

xrt_result_t pacing_app_factory::create(struct u_pacing_app ** out_upa)
{
	std::lock_guard lock(mutex);
	*out_upa = app_pacers.emplace_back(new app_pacer(*this));
	return XRT_SUCCESS;
}

void pacing_app_factory::destroy()
{
}

int64_t pacing_app_factory::get_frame_time()
{
	std::lock_guard lock(mutex);
	if (app_pacers.empty())
		return 1;
	return std::ranges::max(std::ranges::transform_view(app_pacers, [](auto * pacer) { return pacer->get_app_time(); }));
}

} // namespace wivrn
