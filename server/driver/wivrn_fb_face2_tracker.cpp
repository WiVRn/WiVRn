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

#include "wivrn_fb_face2_tracker.h"

#include "wivrn_packets.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include "utils/method.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"

#include <cstdint>
#include <openxr/openxr.h>

namespace wivrn
{

wivrn_fb_face2_tracker::wivrn_fb_face2_tracker(xrt_device * hmd,
                                               wivrn::wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_FB_FACE_TRACKING2,
                .device_type = XRT_DEVICE_TYPE_FACE_TRACKER,
                .str = "WiVRn FB v2 Face Tracker",
                .serial = "WiVRn FB v2 Face Tracker",
                .tracking_origin = hmd->tracking_origin,
                .input_count = 1,
                .inputs = &face_input,
                .supported = {
                        .face_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_fb_face2_tracker::update_inputs>,
                .get_face_tracking = method_pointer<&wivrn_fb_face2_tracker::get_face_tracking>,
                .destroy = [](xrt_device *) {},
        },
        face_input{
                .active = true,
                .name = XRT_INPUT_FB_FACE_TRACKING2_VISUAL,
        },
        cnx(cnx)
{
}

xrt_result_t wivrn_fb_face2_tracker::update_inputs()
{
	return XRT_SUCCESS;
}

void wivrn_fb_face2_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	auto * face = std::get_if<from_headset::tracking::fb_face2>(&tracking.face);
	if (not(face and face->is_valid))
		return;

	wivrn_fb_face2_data data{
	        .weights = face->weights,
	        .confidences = face->confidences,
	        .is_valid = face->is_valid,
	        .is_eye_following_blendshapes_valid = face->is_eye_following_blendshapes_valid,
	        .time = offset.from_headset(face->time),
	};

	face_list.update_tracking(tracking.production_timestamp, tracking.timestamp, data, offset);
}

xrt_result_t wivrn_fb_face2_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
{
	if (facial_expression_type == XRT_INPUT_FB_FACE_TRACKING2_VISUAL)
	{
		auto [production_timestamp, data] = face_list.get_at(at_timestamp_ns);
		cnx.add_tracking_request(device_id::FACE, at_timestamp_ns, production_timestamp);

		inout_value->face_expression_set2_fb.is_valid = data.is_valid;
		inout_value->face_expression_set2_fb.sample_time_ns = data.time;

		if (not data.is_valid)
			return XRT_SUCCESS;

		inout_value->face_expression_set2_fb.is_eye_following_blendshapes_valid = data.is_eye_following_blendshapes_valid;

		std::ranges::copy(data.weights, inout_value->face_expression_set2_fb.weights);
		std::ranges::copy(data.confidences, inout_value->face_expression_set2_fb.confidences);

		inout_value->face_expression_set2_fb.data_source = XRT_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB;

		return XRT_SUCCESS;
	}

	U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), facial_expression_type);
	return XRT_ERROR_INPUT_UNSUPPORTED;
}
} // namespace wivrn
