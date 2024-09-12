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
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "util/u_logging.h"

#include "xrt/xrt_results.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <openxr/openxr.h>

static void wivrn_fb_face2_tracker_destroy(xrt_device * xdev);

static void wivrn_fb_face2_tracker_update_inputs(xrt_device * xdev);

static xrt_result_t wivrn_fb_face2_tracker_get_face_tracking(struct xrt_device * xdev,
                                                             enum xrt_input_name facial_expression_type,
                                                             int64_t at_timestamp_ns,
                                                             struct xrt_facial_expression_set * out_value);

wivrn_fb_face2_tracker::wivrn_fb_face2_tracker(xrt_device * hmd,
                                               std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx) :
        xrt_device{}, cnx(cnx)
{
	xrt_device * base = this;
	base->tracking_origin = hmd->tracking_origin;
	base->get_face_tracking = wivrn_fb_face2_tracker_get_face_tracking;
	base->update_inputs = wivrn_fb_face2_tracker_update_inputs;
	base->destroy = wivrn_fb_face2_tracker_destroy;
	name = XRT_DEVICE_FB_FACE_TRACKING2;
	device_type = XRT_DEVICE_TYPE_FACE_TRACKER;
	face_tracking_supported = true;

	// Print name.
	strcpy(str, "WiVRn FB v2 Face Tracker");
	strcpy(serial, "WiVRn FB v2 Face Tracker");

	// Setup input.
	face_input.name = XRT_INPUT_FB_FACE_TRACKING2_VISUAL;
	face_input.active = true;
	inputs = &face_input;
	input_count = 1;
}

void wivrn_fb_face2_tracker::update_inputs()
{
	// Empty
}

void wivrn_fb_face2_tracker::update_tracking(const from_headset::fb_face2 & face, const clock_offset & offset)
{
	if (!face.is_valid)
		return;

	wivrn_fb_face2_data data;
	data.is_valid = face.is_valid;
	data.is_eye_following_blendshapes_valid = face.is_eye_following_blendshapes_valid;
	data.weights = face.weights;
	data.confidences = face.confidences;

	if (not face_list.update_tracking(face.production_timestamp, face.timestamp, data, offset))
		cnx->set_enabled(to_headset::tracking_control::id::face, false);
}

xrt_result_t wivrn_fb_face2_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
{
	if (facial_expression_type == XRT_INPUT_FB_FACE_TRACKING2_VISUAL)
	{
		cnx->set_enabled(to_headset::tracking_control::id::face, true);
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

/*
 *
 * Functions
 *
 */

static void wivrn_fb_face2_tracker_destroy(xrt_device * xdev)
{
	static_cast<wivrn_fb_face2_tracker *>(xdev)->unregister();
}

static void wivrn_fb_face2_tracker_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_fb_face2_tracker *>(xdev)->update_inputs();
}

static xrt_result_t wivrn_fb_face2_tracker_get_face_tracking(struct xrt_device * xdev,
                                                             enum xrt_input_name facial_expression_type,
                                                             int64_t at_timestamp_ns,
                                                             struct xrt_facial_expression_set * inout_value)
{
	return static_cast<wivrn_fb_face2_tracker *>(xdev)->get_face_tracking(facial_expression_type, at_timestamp_ns, inout_value);
}
