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

#include "pico_face_tracker.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/pico_eye_types.h"
#include "xr/session.h"
#include "xr/xr.h"
#include <openxr/openxr.h>

xr::pico_face_tracker::pico_face_tracker(instance & inst, session & s_) :
        s(s_)
{
	xrStartEyeTrackingPICO = inst.get_proc<PFN_xrStartEyeTrackingPICO>("xrStartEyeTrackingPICO");
	xrStopEyeTrackingPICO = inst.get_proc<PFN_xrStopEyeTrackingPICO>("xrStopEyeTrackingPICO");
	xrSetTrackingModePICO = inst.get_proc<PFN_xrSetTrackingModePICO>("xrSetTrackingModePICO");
	xrGetFaceTrackingStatePICO = inst.get_proc<PFN_xrGetFaceTrackingStatePICO>("xrGetFaceTrackingStatePICO");
	xrGetFaceTrackingDataPICO = inst.get_proc<PFN_xrGetFaceTrackingDataPICO>("xrGetFaceTrackingDataPICO");

	CHECK_XR(xrStartEyeTrackingPICO(s));
	CHECK_XR(xrSetTrackingModePICO(s, XR_TRACKING_MODE_FACE_BIT_PICO));
}
xr::pico_face_tracker::~pico_face_tracker()
{
	if (auto res = xrStopEyeTrackingPICO(s, XR_TRACKING_MODE_FACE_BIT_PICO); !XR_SUCCEEDED(res))
		spdlog::warn("Failed to deactivate face tracking: {}", xr::to_string(res));
}

void xr::pico_face_tracker::get_weights(XrTime time, wivrn::from_headset::tracking::pico_face & out_expressions)
{
	if (!xrGetFaceTrackingDataPICO)
		return;

	XrFaceTrackingDataPICO face_tracking{.time = 0};

	if (auto res = xrGetFaceTrackingDataPICO(s, time, XR_GET_FACE_DATA_DEFAULT_PICO, &face_tracking); res != XR_SUCCESS)
	{
		XrTrackingModeFlagsPICO mode;
		XrTrackingStateCodePICO code;

		auto state_res = xrGetFaceTrackingStatePICO(s, &mode, &code);
		if (XR_SUCCEEDED(state_res))
			spdlog::warn("Unable to get face tracking data: xrGetFaceTrackingDataPICO returned {}, tracking mode state {}, flags {}", xr::to_string(res), xr::to_string(code), mode);
		else
			spdlog::warn("Unable to get face tracking data: xrGetFaceTrackingDataPICO returned {}, unable to get face tracking state: {}", xr::to_string(res), xr::to_string(state_res));

		if (auto res = xrStartEyeTrackingPICO(s); !XR_SUCCEEDED(res))
		{
			spdlog::warn("Failed to start eye tracking: {}", xr::to_string(res));
			goto exit;
		}
		if (auto res = xrSetTrackingModePICO(s, XR_TRACKING_MODE_FACE_BIT_PICO); !XR_SUCCEEDED(res))
		{
			spdlog::warn("Failed to set tracking mode: {}", xr::to_string(res));
		}

exit:
		out_expressions.is_valid = false;
		return;
	}

	std::copy_n(face_tracking.blendShapeWeight, std::size(face_tracking.blendShapeWeight), out_expressions.weights.data());
	out_expressions.is_valid = face_tracking.time != 0;
}
