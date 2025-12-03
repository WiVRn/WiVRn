/*
 * WiVRn VR streaming
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

#include "android_face_tracker.h"
#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include <openxr/openxr.h>

xr::android_face_tracker::android_face_tracker(instance & inst, session & s) :
        handle(inst.get_proc<PFN_xrDestroyFaceTrackerANDROID>("xrDestroyFaceTrackerANDROID"))
{
	auto xrCreateFaceTrackerANDROID = inst.get_proc<PFN_xrCreateFaceTrackerANDROID>("xrCreateFaceTrackerANDROID");
	xrGetFaceCalibrationStateANDROID = inst.get_proc<PFN_xrGetFaceCalibrationStateANDROID>("xrGetFaceCalibrationStateANDROID");
	xrGetFaceStateANDROID = inst.get_proc<PFN_xrGetFaceStateANDROID>("xrGetFaceStateANDROID");
	assert(xrCreateFaceTrackerANDROID);

	XrFaceTrackerCreateInfoANDROID create_info{
	        .type = XR_TYPE_FACE_TRACKER_CREATE_INFO_ANDROID,
	        .next = nullptr,
	};

	CHECK_XR(xrCreateFaceTrackerANDROID(s, &create_info, &id));
}

void xr::android_face_tracker::get_weights(XrTime time, wivrn::from_headset::tracking::android_face & out_expressions)
{
	if (!id || !xrGetFaceStateANDROID)
		return;

	XrBool32 is_calibrated;
	if (XR_SUCCEEDED(xrGetFaceCalibrationStateANDROID(id, &is_calibrated)))
		out_expressions.is_calibrated = is_calibrated;
	else
		out_expressions.is_calibrated = false;

	XrFaceStateGetInfoANDROID info{
	        .type = XR_TYPE_FACE_STATE_GET_INFO_ANDROID,
	        .next = nullptr,
	        .time = time,
	};

	XrFaceStateANDROID state{
	        .type = XR_TYPE_FACE_STATE_ANDROID,
	        .next = nullptr,
	        .parametersCapacityInput = (uint32_t)out_expressions.parameters.size(),
	        .parameters = out_expressions.parameters.data(),
	        .regionConfidencesCapacityInput = (uint32_t)out_expressions.confidences.size(),
	        .regionConfidences = out_expressions.confidences.data(),
	};

	CHECK_XR(xrGetFaceStateANDROID(id, &info, &state));
	out_expressions.state = state.faceTrackingState;
	out_expressions.sample_time = state.sampleTime;
	out_expressions.is_valid = state.isValid;
}
