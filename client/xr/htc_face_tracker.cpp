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

#include "htc_face_tracker.h"
#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include <openxr/openxr.h>

xr::htc_face_tracker::htc_face_tracker(instance & inst, session & s, bool eye, bool lip) :
        s(s)
{
	if (!eye && !lip)
		return;

	xrCreateFacialTrackerHTC = inst.get_proc<PFN_xrCreateFacialTrackerHTC>("xrCreateFacialTrackerHTC");
	xrGetFacialExpressionsHTC = inst.get_proc<PFN_xrGetFacialExpressionsHTC>("xrGetFacialExpressionsHTC");
	xrDestroyFacialTrackerHTC = inst.get_proc<PFN_xrDestroyFacialTrackerHTC>("xrDestroyFacialTrackerHTC");

	XrFacialTrackerCreateInfoHTC create_info{
	        .type = XR_TYPE_FACIAL_TRACKER_CREATE_INFO_HTC,
	        .next = nullptr,
	};

	if (eye)
	{
		create_info.facialTrackingType = XR_FACIAL_TRACKING_TYPE_EYE_DEFAULT_HTC;
		xrCreateFacialTrackerHTC(s, &create_info, &this->eye);
	}
	if (lip)
	{
		create_info.facialTrackingType = XR_FACIAL_TRACKING_TYPE_LIP_DEFAULT_HTC;
		xrCreateFacialTrackerHTC(s, &create_info, &this->lip);
	}
}

xr::htc_face_tracker::~htc_face_tracker()
{
	if (!xrDestroyFacialTrackerHTC)
		return;

	if (eye)
		CHECK_XR(xrDestroyFacialTrackerHTC(std::exchange(eye, nullptr)));
	if (lip)
		CHECK_XR(xrDestroyFacialTrackerHTC(std::exchange(lip, nullptr)));
}

void xr::htc_face_tracker::get_weights(XrTime time, packet_type & out_expressions)
{
	if (!(eye || lip) || !xrGetFacialExpressionsHTC)
		return;

	XrFacialExpressionsHTC expressions{
	        .type = XR_TYPE_FACIAL_EXPRESSIONS_HTC,
	        .next = nullptr,
	        .sampleTime = time,
	};

	if (eye)
	{
		expressions.expressionCount = out_expressions.eye.size();
		expressions.expressionWeightings = out_expressions.eye.data();
		CHECK_XR(xrGetFacialExpressionsHTC(eye, &expressions));
		out_expressions.eye_active = expressions.isActive;
	}
	if (lip)
	{
		expressions.expressionCount = out_expressions.lip.size();
		expressions.expressionWeightings = out_expressions.lip.data();
		CHECK_XR(xrGetFacialExpressionsHTC(lip, &expressions));
		out_expressions.lip_active = expressions.isActive;
	}
}
