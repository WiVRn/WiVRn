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

#include "wivrn_pico_face_tracker.h"

#include "wivrn_packets.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include "xr/pico_eye_types.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <openxr/openxr.h>

namespace wivrn
{

static void wivrn_pico_face_tracker_destroy(xrt_device * xdev);

static xrt_result_t wivrn_pico_face_tracker_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_pico_face_tracker *>(xdev)->update_inputs();
	return XRT_SUCCESS;
}

static xrt_result_t wivrn_pico_face_tracker_get_face_tracking(struct xrt_device * xdev,
                                                              enum xrt_input_name facial_expression_type,
                                                              int64_t at_timestamp_ns,
                                                              struct xrt_facial_expression_set * out_value);

wivrn_pico_face_tracker::wivrn_pico_face_tracker(xrt_device * hmd,
                                                 wivrn::wivrn_session & cnx) :
        xrt_device{}, cnx(cnx)
{
	xrt_device * base = this;
	base->tracking_origin = hmd->tracking_origin;
	base->get_face_tracking = wivrn_pico_face_tracker_get_face_tracking;
	base->update_inputs = wivrn_pico_face_tracker_update_inputs;
	base->destroy = wivrn_pico_face_tracker_destroy;
	name = XRT_DEVICE_FB_FACE_TRACKING2;
	device_type = XRT_DEVICE_TYPE_FACE_TRACKER;
	face_tracking_supported = true;

	// Print name.
	strcpy(str, "WiVRn Pico Face Tracker (as FB v2)");
	strcpy(serial, "WiVRn Pico Face Tracker");

	// Setup input.
	face_input.name = XRT_INPUT_FB_FACE_TRACKING2_VISUAL;
	face_input.active = true;
	inputs = &face_input;
	input_count = 1;
}

void wivrn_pico_face_tracker::update_inputs()
{
	// Empty
}

void wivrn_pico_face_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	static std::array<float, XR_FACE_CONFIDENCE2_COUNT_FB> confidences{1.0, 1.0};
	if (not(tracking.face_pico and tracking.face_pico->is_valid))
		return;
	const auto & face = *tracking.face_pico;

	std::array<float, XR_FACE_EXPRESSION2_COUNT_FB> weights{};

#define MAP_EXPRESSION(fb, pico) \
	weights[fb] = face.weights[pico];

	// Map the blendshapes.
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_BROW_LOWERER_L_FB, XR_BS_BROWDOWN_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_BROW_LOWERER_R_FB, XR_BS_BROWDOWN_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_CHEEK_PUFF_L_FB, XR_BS_CHEEKPUFF_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_CHEEK_PUFF_R_FB, XR_BS_CHEEKPUFF_PICO);
	// XR_FACE_EXPRESSION2_CHEEK_RAISER_L_FB
	// XR_FACE_EXPRESSION2_CHEEK_RAISER_R_FB
	// XR_FACE_EXPRESSION2_CHEEK_SUCK_L_FB
	// XR_FACE_EXPRESSION2_CHEEK_SUCK_R_FB
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_CHIN_RAISER_B_FB, XR_BS_MOUTHSHRUGLOWER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_CHIN_RAISER_T_FB, XR_BS_MOUTHSHRUGUPPER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_DIMPLER_L_FB, XR_BS_MOUTHDIMPLE_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_DIMPLER_R_FB, XR_BS_MOUTHDIMPLE_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_CLOSED_L_FB, XR_BS_EYEBLINK_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_CLOSED_R_FB, XR_BS_EYEBLINK_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_DOWN_L_FB, XR_BS_EYELOOKDOWN_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_DOWN_R_FB, XR_BS_EYELOOKDOWN_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_LEFT_L_FB, XR_BS_EYELOOKIN_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_LEFT_R_FB, XR_BS_EYELOOKIN_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_RIGHT_L_FB, XR_BS_EYELOOKOUT_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_RIGHT_R_FB, XR_BS_EYELOOKOUT_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_UP_L_FB, XR_BS_EYELOOKUP_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_EYES_LOOK_UP_R_FB, XR_BS_EYELOOKUP_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_INNER_BROW_RAISER_L_FB, XR_BS_BROWINNERUP_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_INNER_BROW_RAISER_R_FB, XR_BS_BROWINNERUP_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_JAW_DROP_FB, XR_BS_JAWOPEN_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_JAW_SIDEWAYS_LEFT_FB, XR_BS_JAWLEFT_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_JAW_SIDEWAYS_RIGHT_FB, XR_BS_JAWRIGHT_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_JAW_THRUST_FB, XR_BS_JAWFORWARD_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LID_TIGHTENER_L_FB, XR_BS_EYESQUINT_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB, XR_BS_EYESQUINT_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_L_FB, XR_BS_MOUTHFROWN_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_R_FB, XR_BS_MOUTHFROWN_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_L_FB, XR_BS_MOUTHSMILE_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_R_FB, XR_BS_MOUTHSMILE_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_FUNNELER_LB_FB, XR_BS_MOUTHFUNNEL_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_FUNNELER_LT_FB, XR_BS_MOUTHFUNNEL_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_FUNNELER_RB_FB, XR_BS_MOUTHFUNNEL_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_FUNNELER_RT_FB, XR_BS_MOUTHFUNNEL_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_PRESSOR_L_FB, XR_BS_MOUTHPRESS_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_PRESSOR_R_FB, XR_BS_MOUTHPRESS_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_PUCKER_L_FB, XR_BS_MOUTHPUCKER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_PUCKER_R_FB, XR_BS_MOUTHPUCKER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_STRETCHER_L_FB, XR_BS_MOUTHSTRETCH_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_STRETCHER_R_FB, XR_BS_MOUTHSTRETCH_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_SUCK_LB_FB, XR_BS_MOUTHROLLLOWER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_SUCK_LT_FB, XR_BS_MOUTHROLLLOWER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_SUCK_RT_FB, XR_BS_MOUTHROLLUPPER_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIP_SUCK_RB_FB, XR_BS_MOUTHROLLUPPER_PICO);
	// XR_FACE_EXPRESSION2_LIP_TIGHTENER_L_FB
	// XR_FACE_EXPRESSION2_LIP_TIGHTENER_R_FB
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LIPS_TOWARD_FB, XR_BS_MOUTHCLOSE_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LOWER_LIP_DEPRESSOR_L_FB, XR_BS_MOUTHLOWERDOWN_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_LOWER_LIP_DEPRESSOR_R_FB, XR_BS_MOUTHLOWERDOWN_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_MOUTH_LEFT_FB, XR_BS_MOUTHLEFT_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_MOUTH_RIGHT_FB, XR_BS_MOUTHRIGHT_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_NOSE_WRINKLER_L_FB, XR_BS_NOSESNEER_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_NOSE_WRINKLER_R_FB, XR_BS_NOSESNEER_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_OUTER_BROW_RAISER_L_FB, XR_BS_BROWOUTERUP_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_OUTER_BROW_RAISER_R_FB, XR_BS_BROWOUTERUP_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_UPPER_LID_RAISER_L_FB, XR_BS_EYEWIDE_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_UPPER_LID_RAISER_R_FB, XR_BS_EYEWIDE_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_UPPER_LIP_RAISER_L_FB, XR_BS_MOUTHUPPERUP_L_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_UPPER_LIP_RAISER_R_FB, XR_BS_MOUTHUPPERUP_R_PICO);
	MAP_EXPRESSION(XR_FACE_EXPRESSION2_TONGUE_OUT_FB, XR_BS_TONGUEOUT_PICO);
	// XR_FACE_EXPRESSION2_TONGUE_TIP_INTERDENTAL_FB
	// XR_FACE_EXPRESSION2_TONGUE_TIP_ALVEOLAR_FB
	// XR_FACE_EXPRESSION2_TONGUE_FRONT_DORSAL_PALATE_FB
	// XR_FACE_EXPRESSION2_TONGUE_MID_DORSAL_PALATE_FB
	// XR_FACE_EXPRESSION2_TONGUE_BACK_DORSAL_VELAR_FB
	// XR_FACE_EXPRESSION2_TONGUE_RETREAT_FB

#undef MAP_EXPRESSION

	wivrn_fb_face2_data data{
	        .weights = weights,
	        .confidences = confidences,
	        .is_valid = face.is_valid,
	        .is_eye_following_blendshapes_valid = face.is_valid,
	};

	if (not face_list.update_tracking(tracking.production_timestamp, tracking.timestamp, data, offset))
		cnx.set_enabled(to_headset::tracking_control::id::face, false);
}

xrt_result_t wivrn_pico_face_tracker::get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * inout_value)
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

/*
 *
 * Functions
 *
 */

static void wivrn_pico_face_tracker_destroy(xrt_device * xdev)
{
}

static xrt_result_t wivrn_pico_face_tracker_get_face_tracking(struct xrt_device * xdev,
                                                              enum xrt_input_name facial_expression_type,
                                                              int64_t at_timestamp_ns,
                                                              struct xrt_facial_expression_set * inout_value)
{
	return static_cast<wivrn_pico_face_tracker *>(xdev)->get_face_tracking(facial_expression_type, at_timestamp_ns, inout_value);
}
} // namespace wivrn
