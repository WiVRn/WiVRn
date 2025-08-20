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
#include "xr/instance.h"
#include "xr/session.h"
#include <openxr/openxr.h>

static XrFacialTrackerHTC make_tracker(xr::instance & inst, xr::session & s, XrFacialTrackingTypeHTC type)
{
	XrFacialTrackerCreateInfoHTC create_info{
	        .type = XR_TYPE_FACIAL_TRACKER_CREATE_INFO_HTC,
	        .facialTrackingType = type,
	};
	auto xrCreateFacialTrackerHTC = inst.get_proc<PFN_xrCreateFacialTrackerHTC>("xrCreateFacialTrackerHTC");
	XrFacialTrackerHTC id;
	CHECK_XR(xrCreateFacialTrackerHTC(s, &create_info, &id));
	return id;
}

static XrFacialTrackerHTC get_eye_tracker(xr::instance & inst, xr::session & s)
{
	static XrFacialTrackerHTC res = make_tracker(inst, s, XR_FACIAL_TRACKING_TYPE_EYE_DEFAULT_HTC);
	return res;
}
static XrFacialTrackerHTC get_lip_tracker(xr::instance & inst, xr::session & s)
{
	static XrFacialTrackerHTC res = make_tracker(inst, s, XR_FACIAL_TRACKING_TYPE_LIP_DEFAULT_HTC);
	return res;
}

xr::htc_face_tracker::htc_face_tracker(instance & inst, session & s, bool eye, bool lip) :
        xrGetFacialExpressionsHTC(inst.get_proc<PFN_xrGetFacialExpressionsHTC>("xrGetFacialExpressionsHTC")),
        eye(eye ? get_eye_tracker(inst, s) : XR_NULL_HANDLE),
        lip(lip ? get_lip_tracker(inst, s) : XR_NULL_HANDLE)
{
}

void xr::htc_face_tracker::get_weights(XrTime time, packet_type & out_expressions)
{
	assert(xrGetFacialExpressionsHTC);

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
