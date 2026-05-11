/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "polynomial_interpolator.h"
#include "utils/csv_logger.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"

#include <atomic>
#include <mutex>
#include <optional>

namespace wivrn
{
struct clock_offset;

class pose_list
{
	std::atomic<pose_list *> source = nullptr;
	xrt_pose offset;
	std::atomic_bool derive_forced = false;
	std::mutex mutex;

	polynomial_interpolator<3> positions;
	polynomial_interpolator<4, true> orientations;

	struct debug_data
	{
		bool in; // true: received data, false: data request
		XrTime production_timestamp;
		XrTime timestamp;
		XrTime now;
		std::array<float, 3> position;
		std::array<float, 3> dposition;
		std::array<float, 4> orientation;
		std::array<float, 4> dorientation;
	};

	std::optional<csv_logger<debug_data>> dumper;

public:
	const wivrn::device_id device;

	static xrt_space_relation interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t);
	static xrt_space_relation extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t);

	pose_list(wivrn::device_id id);

	std::pair<XrTime, XrTime> get_bounds() const;

	void update_tracking(const wivrn::from_headset::tracking &, const clock_offset & offset);
	void set_derived(pose_list * source, xrt_pose offset, bool force = false);

	std::tuple<XrTime, xrt_space_relation, device_id> get_pose_at(XrTime at_timestamp_ns);

	void reset();
	std::pair<XrTime, xrt_space_relation> get_at(XrTime at_timestamp_ns);

private:
	void add_sample(XrTime production_timestamp, XrTime timestamp, const from_headset::tracking::pose & pose, const clock_offset & offset);
};
} // namespace wivrn
