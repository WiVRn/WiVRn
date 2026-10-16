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

#pragma once

#include "util/u_pacing.h"

#include <deque>
#include <mutex>
#include <vector>

namespace wivrn
{

class wivrn_session;
class app_pacer;

class pacing_app_factory : public u_pacing_app_factory
{
	friend class app_pacer;
	std::mutex mutex;
	std::vector<app_pacer *> app_pacers;
	void remove_app(app_pacer *);

	// Timestamps of mark_delivered calls from the base (non-overlay) session's pacer only:
	// a real per-application "the game just called xrEndFrame" signal, unlike the system
	// compositor's own commit rate which also includes any overlay (e.g. WayVR).
	std::deque<int64_t> base_session_delivered;
	void record_base_session_delivered(int64_t when_ns);

public:
	using base_t = u_pacing_app_factory;

	pacing_app_factory();
	xrt_result_t create(bool is_overlay, struct u_pacing_app ** out_upa);
	void destroy();

	int64_t get_frame_time();

	// Windowed average fps of the base session's real frame delivery, 0 if no data yet.
	float base_session_fps();
};

} // namespace wivrn
