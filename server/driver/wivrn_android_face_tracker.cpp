/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2024  galister <galister@librevr.org>
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_android_face_tracker.h"

#include "wivrn_packets.h"
#include "wivrn_session.h"

#include "os/os_time.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace wivrn
{

wivrn_android_face_tracker::wivrn_android_face_tracker(xrt_device * hmd,
                                                       wivrn::wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_ANDROID_FACE_TRACKING,
                .device_type = XRT_DEVICE_TYPE_FACE_TRACKER,
                .str = "WiVRn Android Face Tracker",
                .serial = "WiVRn Android Face Tracker",
                .tracking_origin = hmd->tracking_origin,
                .input_count = 1,
                .inputs = &face_input,
                .supported = {
                        .face_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_android_face_tracker::update_inputs>,
                .get_face_tracking = method_pointer<&wivrn_android_face_tracker::get_face_tracking>,
                .get_face_calibration_state_android = method_pointer<&wivrn_android_face_tracker::get_face_calibration_state_android>,
                .destroy = [](xrt_device *) {},
        },
        face_input{
                .active = true,
                .name = XRT_INPUT_ANDROID_FACE_TRACKING,
        },
        cnx(cnx)
{
}

xrt_result_t wivrn_android_face_tracker::update_inputs()
{
	return XRT_SUCCESS;
}

void wivrn_android_face_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	auto * face = std::get_if<from_headset::tracking::android_face>(&tracking.face);
	if (not(face and face->is_valid))
		return;

	wivrn_android_face_data data{
	        .parameters = face->parameters,
	        .confidences = face->confidences,
	        .state = (xrt_face_tracking_state_android)face->state,
	        .sample_time = offset.from_headset(face->sample_time),
	        .is_calibrated = face->is_calibrated,
	        .is_valid = face->is_valid,
	};

	if (not face_list.update_tracking(tracking.production_timestamp, tracking.timestamp, data, offset))
		cnx.set_enabled(to_headset::tracking_control::id::face, false);
}

xrt_result_t wivrn_android_face_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
{
	if (facial_expression_type == XRT_INPUT_ANDROID_FACE_TRACKING)
	{
		cnx.set_enabled(to_headset::tracking_control::id::face, true);
		auto [_, data] = face_list.get_at(at_timestamp_ns);

		inout_value->face_expression_set_android.state = data.state;
		inout_value->face_expression_set_android.is_valid = data.is_valid;
		inout_value->face_expression_set_android.sample_time_ns = data.sample_time;

		if (not data.is_valid)
			return XRT_SUCCESS;

		std::ranges::copy(data.parameters, inout_value->face_expression_set_android.parameters);
		std::ranges::copy(data.confidences, inout_value->face_expression_set_android.region_confidences);

		return XRT_SUCCESS;
	}

	U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), facial_expression_type);
	return XRT_ERROR_INPUT_UNSUPPORTED;
}

xrt_result_t wivrn_android_face_tracker::get_face_calibration_state_android(bool * out_face_is_calibrated)
{
	auto [_, data] = face_list.get_at(os_monotonic_get_ns());
	*out_face_is_calibrated = data.is_calibrated;
	return XRT_SUCCESS;
}
} // namespace wivrn