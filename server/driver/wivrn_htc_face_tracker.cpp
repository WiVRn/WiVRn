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

#include "wivrn_htc_face_tracker.h"

#include "wivrn_packets.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <openxr/openxr.h>

namespace wivrn
{

static void wivrn_htc_face_tracker_destroy(xrt_device * xdev);

static xrt_result_t wivrn_htc_face_tracker_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_htc_face_tracker *>(xdev)->update_inputs();
	return XRT_SUCCESS;
}

static xrt_result_t wivrn_htc_face_tracker_get_face_tracking(struct xrt_device * xdev,
                                                             enum xrt_input_name facial_expression_type,
                                                             int64_t at_timestamp_ns,
                                                             struct xrt_facial_expression_set * out_value);

wivrn_htc_face_tracker::wivrn_htc_face_tracker(xrt_device * hmd,
                                               wivrn::wivrn_session & cnx) :
        xrt_device{}, cnx(cnx)
{
	xrt_device * base = this;
	base->tracking_origin = hmd->tracking_origin;
	base->get_face_tracking = wivrn_htc_face_tracker_get_face_tracking;
	base->update_inputs = wivrn_htc_face_tracker_update_inputs;
	base->destroy = wivrn_htc_face_tracker_destroy;
	name = XRT_DEVICE_HTC_FACE_TRACKING;
	device_type = XRT_DEVICE_TYPE_FACE_TRACKER;
	face_tracking_supported = true;

	// Print name.
	strcpy(str, "WiVRn HTC Face Tracker");
	strcpy(serial, "WiVRn HTC Face Tracker");

	// Setup input.
	inputs_array[0].name = XRT_INPUT_HTC_EYE_FACE_TRACKING;
	inputs_array[0].active = true;

	inputs_array[1].name = XRT_INPUT_HTC_LIP_FACE_TRACKING;
	inputs_array[1].active = true;

	inputs = inputs_array.data();
	input_count = inputs_array.size();
}

void wivrn_htc_face_tracker::update_inputs()
{
	// Empty
}

void wivrn_htc_face_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	if (not(tracking.face_htc))
		return;
	const auto & face = *tracking.face_htc;

	wivrn_htc_face_data data{
	        .eye = face.eye,
	        .lip = face.lip,
	        .eye_active = face.eye_active,
	        .lip_active = face.lip_active};

	if (not face_list.update_tracking(tracking.production_timestamp, tracking.timestamp, data, offset))
		cnx.set_enabled(to_headset::tracking_control::id::face, false);
}

xrt_result_t wivrn_htc_face_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
{
	if (facial_expression_type == XRT_INPUT_HTC_EYE_FACE_TRACKING)
	{
		cnx.set_enabled(to_headset::tracking_control::id::face, true);
		auto [_, data] = face_list.get_at(at_timestamp_ns);

		inout_value->base_expression_set_htc.is_active = data.eye_active;
		inout_value->base_expression_set_htc.sample_time_ns = at_timestamp_ns;

		if (not data.eye_active)
			return XRT_SUCCESS;

		memcpy(&inout_value->eye_expression_set_htc.expression_weights, data.eye.data(), sizeof(float) * data.eye.size());

		return XRT_SUCCESS;
	}
	else if (facial_expression_type == XRT_INPUT_HTC_LIP_FACE_TRACKING)
	{
		cnx.set_enabled(to_headset::tracking_control::id::face, true);
		auto [_, data] = face_list.get_at(at_timestamp_ns);

		inout_value->base_expression_set_htc.is_active = data.lip_active;
		inout_value->base_expression_set_htc.sample_time_ns = at_timestamp_ns;

		if (not data.lip_active)
			return XRT_SUCCESS;

		memcpy(&inout_value->lip_expression_set_htc.expression_weights, data.lip.data(), sizeof(float) * data.lip.size());

		return XRT_SUCCESS;
	}

	return XRT_ERROR_NOT_IMPLEMENTED;
}

/*
 *
 * Functions
 *
 */

static void wivrn_htc_face_tracker_destroy(xrt_device * xdev)
{
}

static xrt_result_t wivrn_htc_face_tracker_get_face_tracking(struct xrt_device * xdev,
                                                             enum xrt_input_name facial_expression_type,
                                                             int64_t at_timestamp_ns,
                                                             struct xrt_facial_expression_set * inout_value)
{
	return static_cast<wivrn_htc_face_tracker *>(xdev)->get_face_tracking(facial_expression_type, at_timestamp_ns, inout_value);
}
} // namespace wivrn
