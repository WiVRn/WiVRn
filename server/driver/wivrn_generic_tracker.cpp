/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_generic_tracker.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "wivrn_session.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_results.h"
#include <format>

namespace wivrn
{

xrt_space_relation tracker_pose_list::interpolate(const xrt_space_relation & a, const xrt_space_relation & b, float t)
{
	return pose_list::interpolate(a, b, t);
}

xrt_space_relation tracker_pose_list::extrapolate(const xrt_space_relation & a, const xrt_space_relation & b, int64_t ta, int64_t tb, int64_t t)
{
	return pose_list::extrapolate(a, b, ta, tb, t);
}

void tracker_pose_list::update_tracking(XrTime produced_timestamp, XrTime timestamp, const xrt_space_relation & pose, const clock_offset & offset)
{
	add_sample(produced_timestamp, timestamp, pose, offset);
}

wivrn_generic_tracker::wivrn_generic_tracker(std::string name, xrt_device * hmd, wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_VIVE_TRACKER,
                .device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER,
                .hmd = nullptr,
                .tracking_origin = hmd->tracking_origin,
                .supported = {
                        .orientation_tracking = true,
                        .position_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_generic_tracker::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_generic_tracker::get_tracked_pose>,
                .destroy = [](xrt_device *) {},
        },
        cnx(cnx)
{
	auto unique_name = std::format("WiVRn generic tracker ({})", name);
	strlcpy(str, unique_name.c_str(), std::size(str));
	{
		std::string serial{};
		serial.reserve(name.size());
		std::ranges::transform(name, std::back_inserter(serial), [](auto c) { return tolower(c); });
		std::ranges::replace(serial, ' ', '-');
		auto unique_serial = std::format("wivrn-{}", serial);
		strlcpy(this->serial, unique_serial.c_str(), std::size(this->serial));
	}

	pose_input.name = XRT_INPUT_GENERIC_TRACKER_POSE;
	pose_input.active = true;

	inputs = &pose_input;
	input_count = 1;
}

xrt_result_t wivrn_generic_tracker::update_inputs()
{
	return XRT_SUCCESS;
}

xrt_result_t wivrn_generic_tracker::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * res)
{
	XrTime production_timestamp;

	if (!enabled)
	{
		*res = XRT_SPACE_RELATION_ZERO;
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	if (name == XRT_INPUT_GENERIC_TRACKER_POSE)
	{
		std::tie(production_timestamp, *res) = poses.get_at(at_timestamp_ns);

		cnx.add_tracking_request(device_id::BODY, at_timestamp_ns, production_timestamp);
		return XRT_SUCCESS;
	}

	U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
	return XRT_ERROR_INPUT_UNSUPPORTED;
}

void wivrn_generic_tracker::update_tracking(XrTime produced_timestamp, XrTime timestamp, const xrt_space_relation & pose, const clock_offset & offset)
{
	poses.update_tracking(produced_timestamp, timestamp, pose, offset);
}

void wivrn_generic_tracker::set_enabled(bool enabled)
{
	this->enabled = enabled;
}
} // namespace wivrn
