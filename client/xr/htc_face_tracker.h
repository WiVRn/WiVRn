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

#pragma once

#include "utils/handle.h"
#include "wivrn_packets.h"
#include <openxr/openxr.h>

namespace xr
{
class instance;

XrResult destroy_htc_face_tracker(XrFacialTrackerHTC);

class htc_face_tracker : public utils::handle<XrFacialTrackerHTC, destroy_htc_face_tracker>
{
	PFN_xrGetFacialExpressionsHTC xrGetFacialExpressionsHTC{};
	PFN_xrDestroyFacialTrackerHTC xrDestroyFacialTrackerHTC{};
	XrFacialTrackingTypeHTC trackerType{};

public:
	htc_face_tracker() = default;
	htc_face_tracker(instance & inst, XrFacialTrackerHTC h, XrFacialTrackingTypeHTC facialTrackingTypeHTC);

	void get_weights(XrTime time, struct wivrn::from_headset::tracking::htc_face & out_expressions);
};
} // namespace xr
