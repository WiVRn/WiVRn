/*
 * WiVRn VR streaming
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

#include "fb_face_tracker2.h"
#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include <cstdint>
#include <openxr/openxr.h>

xr::fb_face_tracker2::fb_face_tracker2(instance & inst, session & s) :
        handle(inst.get_proc<PFN_xrDestroyFaceTracker2FB>("xrDestroyFaceTracker2FB"))
{
	auto xrCreateFaceTracker2FB = inst.get_proc<PFN_xrCreateFaceTracker2FB>("xrCreateFaceTracker2FB");
	xrGetFaceExpressionWeights2FB = inst.get_proc<PFN_xrGetFaceExpressionWeights2FB>("xrGetFaceExpressionWeights2FB");
	assert(xrCreateFaceTracker2FB);

	XrFaceTrackingDataSource2FB data_sources[1];
	data_sources[0] = XR_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB;

	XrFaceTrackerCreateInfo2FB create_info{
	        .type = XR_TYPE_FACE_TRACKER_CREATE_INFO2_FB,
	        .next = nullptr,
	        .faceExpressionSet = XR_FACE_EXPRESSION_SET2_DEFAULT_FB,
	        .requestedDataSourceCount = 1,
	        .requestedDataSources = data_sources,
	};

	CHECK_XR(xrCreateFaceTracker2FB(s, &create_info, &id));
}

void xr::fb_face_tracker2::get_weights(XrTime time, wivrn::from_headset::tracking::fb_face2 & out_expressions)
{
	if (!id || !xrGetFaceExpressionWeights2FB)
		return;

	XrFaceExpressionInfo2FB info{
	        .type = XR_TYPE_FACE_EXPRESSION_INFO2_FB,
	        .next = nullptr,
	        .time = time,
	};

	XrFaceExpressionWeights2FB expression_weights{
	        .type = XR_TYPE_FACE_EXPRESSION_WEIGHTS2_FB,
	        .next = nullptr,
	        .weightCount = (uint32_t)out_expressions.weights.size(),
	        .weights = out_expressions.weights.data(),
	        .confidenceCount = (uint32_t)out_expressions.confidences.size(),
	        .confidences = out_expressions.confidences.data(),
	};

	if (XR_SUCCEEDED(xrGetFaceExpressionWeights2FB(id, &info, &expression_weights)))
	{
		out_expressions.is_valid = expression_weights.isValid;
		out_expressions.is_eye_following_blendshapes_valid = expression_weights.isEyeFollowingBlendshapesValid;
	}
	else
	{
		out_expressions.is_valid = false;
		out_expressions.is_eye_following_blendshapes_valid = false;
	}
}
