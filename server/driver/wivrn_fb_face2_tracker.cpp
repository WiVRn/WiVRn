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

#include <cmath>
#include <cstdint>
#include <cstring>
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
                .face_tracking_supported = true,
                .update_inputs = method_pointer<&wivrn_fb_face2_tracker::update_inputs>,
                .get_face_tracking = method_pointer<&wivrn_fb_face2_tracker::get_face_tracking>,
                .destroy = [](xrt_device *) {},
        },
        cnx(cnx)
{
	// Setup input.
	face_input.name = XRT_INPUT_FB_FACE_TRACKING2_VISUAL;
	face_input.active = true;
	inputs = &face_input;
	input_count = 1;
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
	};

	if (not face_list.update_tracking(tracking.production_timestamp, tracking.timestamp, data, offset))
		cnx.set_enabled(to_headset::tracking_control::id::face, false);
}

xrt_result_t wivrn_fb_face2_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
{
	if (facial_expression_type == XRT_INPUT_FB_FACE_TRACKING2_VISUAL)
	{
		cnx.set_enabled(to_headset::tracking_control::id::face, true);
		auto [_, data] = face_list.get_at(at_timestamp_ns);

		inout_value->face_expression_set2_fb.is_valid = data.is_valid;

		if (not data.is_valid)
			return XRT_SUCCESS;

		inout_value->face_expression_set2_fb.is_eye_following_blendshapes_valid = data.is_eye_following_blendshapes_valid;

		memcpy(&inout_value->face_expression_set2_fb.weights, data.weights.data(), sizeof(float) * data.weights.size());
		memcpy(&inout_value->face_expression_set2_fb.confidences, data.confidences.data(), sizeof(float) * data.confidences.size());

		inout_value->face_expression_set2_fb.data_source = XRT_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB;

		return XRT_SUCCESS;
	}

	return XRT_ERROR_NOT_IMPLEMENTED;
}
} // namespace wivrn
