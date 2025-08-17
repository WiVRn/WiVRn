/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "face_tracker.h"

#include "hardware.h"
#include "xr/instance.h"
#include "xr/system.h"

xr::face_tracker_type xr::face_tracker_supported(xr::instance & instance, xr::system & system)
{
	if (instance.has_extension(XR_FB_FACE_TRACKING2_EXTENSION_NAME))
	{
		auto properties = system.fb_face_tracking2_properties();
		if (properties.supportsVisualFaceTracking)
			return xr::face_tracker_type::fb;
	}

	if (instance.has_extension(XR_HTC_FACIAL_TRACKING_EXTENSION_NAME))
	{
		auto properties = system.htc_face_tracking_properties();
		if (properties.supportEyeFacialTracking or properties.supportLipFacialTracking)
			return xr::face_tracker_type::htc;
	}

	switch (guess_model())
	{
		case model::pico_4_pro:
		case model::pico_4_enterprise:
			// The extension used by Pico is not published
			// it doesn't even need to be requested...
			if (instance.has_extension(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) and
			    system.eye_gaze_interaction_properties().supportsEyeGazeInteraction)
				return xr::face_tracker_type::pico;
			break;
		default:
			break;
	}

	return xr::face_tracker_type::none;
}

xr::face_tracker xr::make_face_tracker(xr::instance & instance, xr::system & system, xr::session & session)
{
	if (instance.has_extension(XR_FB_FACE_TRACKING2_EXTENSION_NAME))
	{
		auto properties = system.fb_face_tracking2_properties();
		if (properties.supportsVisualFaceTracking)
			return xr::face_tracker(std::in_place_type_t<xr::fb_face_tracker2>(), instance, session);
	}

	if (instance.has_extension(XR_HTC_FACIAL_TRACKING_EXTENSION_NAME))
	{
		auto properties = system.htc_face_tracking_properties();
		if (properties.supportEyeFacialTracking or properties.supportLipFacialTracking)
			return xr::face_tracker(
			        std::in_place_type_t<xr::htc_face_tracker>(),
			        instance,
			        session,
			        properties.supportEyeFacialTracking,
			        properties.supportLipFacialTracking);
	}

	switch (guess_model())
	{
		case model::pico_4_pro:
		case model::pico_4_enterprise:
			// The extension used by Pico is not published
			// it doesn't even need to be requested...
			if (instance.has_extension(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) and
			    system.eye_gaze_interaction_properties().supportsEyeGazeInteraction)
				return xr::face_tracker(std::in_place_type_t<xr::pico_face_tracker>(), instance, session);
			break;
		default:
			break;
	}

	return std::monostate();
}
