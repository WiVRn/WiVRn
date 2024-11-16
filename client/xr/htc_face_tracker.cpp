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

#include "htc_face_tracker.h"
#include "wivrn_packets.h"
#include "xr/instance.h"
#include <openxr/openxr.h>

static PFN_xrDestroyFacialTrackerHTC xrDestroyFacialTrackerHTC{};

XrResult xr::destroy_htc_face_tracker(XrFacialTrackerHTC id)
{
	return xrDestroyFacialTrackerHTC(id);
}

xr::htc_face_tracker::htc_face_tracker(instance & inst, XrFacialTrackerHTC h, XrFacialTrackingTypeHTC facialTrackingTypeHTC)
{
	id = h;
	xrGetFacialExpressionsHTC = inst.get_proc<PFN_xrGetFacialExpressionsHTC>("xrGetFacialExpressionsHTC");
	xrDestroyFacialTrackerHTC = inst.get_proc<PFN_xrDestroyFacialTrackerHTC>("xrDestroyFacialTrackerHTC");
	trackerType = facialTrackingTypeHTC;
}

void xr::htc_face_tracker::get_weights(XrTime time, wivrn::from_headset::tracking::htc_face & out_expressions)
{
	if (!id || !xrGetFacialExpressionsHTC)
		return;

	XrFacialExpressionsHTC expressions{
	        .type = XR_TYPE_FACIAL_EXPRESSIONS_HTC,
	        .next = nullptr,
	        .sampleTime = time,
	};

	bool * is_active;

	switch (trackerType)
	{
		case XR_FACIAL_TRACKING_TYPE_EYE_DEFAULT_HTC:
			expressions.expressionCount = out_expressions.eye.size();
			expressions.expressionWeightings = out_expressions.eye.data();
			is_active = &out_expressions.eye_active;
			break;
		case XR_FACIAL_TRACKING_TYPE_LIP_DEFAULT_HTC:
			expressions.expressionCount = out_expressions.lip.size();
			expressions.expressionWeightings = out_expressions.lip.data();
			is_active = &out_expressions.lip_active;
			break;
		default:
			throw std::system_error(1, std::generic_category(), "htc_face_tracker with unknown trackerType");
	}

	CHECK_XR(xrGetFacialExpressionsHTC(id, &expressions));

	*is_active = expressions.isActive;
}
