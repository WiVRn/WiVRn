/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2024  galister <galister@librevr.org>
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

#include "wivrn_eye_tracker.h"

#include "wivrn_packets.h"
#include "wivrn_session.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "util/u_logging.h"
#include "utils/method.h"

#include <cstdint>
#include <openxr/openxr.h>

namespace wivrn
{

wivrn_eye_tracker::wivrn_eye_tracker(xrt_device * hmd, wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_EYE_GAZE_INTERACTION,
                .device_type = XRT_DEVICE_TYPE_EYE_TRACKER,
                .str = "WiVRn Eye Tracker",
                .serial = "WiVRn Eye Tracker",
                .tracking_origin = hmd->tracking_origin,
                .input_count = 1,
                .inputs = &gaze_input,
                .supported = {
                        .eye_gaze = true,
                },
                .update_inputs = method_pointer<&wivrn_eye_tracker::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_eye_tracker::get_tracked_pose>,
                .destroy = [](xrt_device *) {},
        },
        gaze_input{
                .active = true,
                .name = XRT_INPUT_GENERIC_EYE_GAZE_POSE,
        },
        gaze(device_id::EYE_GAZE),
        cnx(cnx)
{
}

xrt_result_t wivrn_eye_tracker::update_inputs()
{
	return XRT_SUCCESS;
}

xrt_result_t wivrn_eye_tracker::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * out_relation)
{
	if (name == XRT_INPUT_GENERIC_EYE_GAZE_POSE)
	{
		auto [production_timestamp, relation] = gaze.get_at(at_timestamp_ns);
		*out_relation = relation;
		cnx.add_tracking_request(device_id::EYE_GAZE, at_timestamp_ns, production_timestamp);
		return XRT_SUCCESS;
	}

	U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
	return XRT_ERROR_INPUT_UNSUPPORTED;
}

void wivrn_eye_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	gaze.update_tracking(tracking, offset);
}
} // namespace wivrn
