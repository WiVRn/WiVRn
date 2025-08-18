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

#include "body_tracker.h"

#include "xr/htc_exts.h"
#include "xr/instance.h"
#include "xr/system.h"

xr::body_tracker_type xr::body_tracker_supported(xr::instance & instance, xr::system & system)
{
	if (instance.has_extension(XR_FB_BODY_TRACKING_EXTENSION_NAME) and
	    instance.has_extension(XR_META_BODY_TRACKING_FULL_BODY_EXTENSION_NAME) and
	    instance.has_extension(XR_META_BODY_TRACKING_FIDELITY_EXTENSION_NAME))
	{
		auto fb_body_properties = system.fb_body_tracking_properties();
		if (fb_body_properties.supportsBodyTracking)
			return body_tracker_type::fb;
	}

	if (instance.has_extension(XR_HTC_PATH_ENUMERATION_EXTENSION_NAME) and
	    instance.has_extension(XR_HTC_VIVE_XR_TRACKER_INTERACTION_EXTENSION_NAME))
		return body_tracker_type::htc;

	if (instance.has_extension(XR_BD_BODY_TRACKING_EXTENSION_NAME))
	{
		auto bd_body_properties = system.bd_body_tracking_properties();
		if (bd_body_properties.supportsBodyTracking)
			return body_tracker_type::pico;
	}

	return xr::body_tracker_type::none;
}

xr::body_tracker xr::make_body_tracker(xr::instance & instance, xr::system & system, xr::session & session, std::vector<std::pair<XrPath, xr::space>> & generic_trackers, bool full_body, bool hips)
{
	if (instance.has_extension(XR_FB_BODY_TRACKING_EXTENSION_NAME) and
	    instance.has_extension(XR_META_BODY_TRACKING_FULL_BODY_EXTENSION_NAME) and
	    instance.has_extension(XR_META_BODY_TRACKING_FIDELITY_EXTENSION_NAME))
	{
		auto fb_body_properties = system.fb_body_tracking_properties();
		if (fb_body_properties.supportsBodyTracking)
			return xr::body_tracker(std::in_place_type_t<xr::fb_body_tracker>(),
			                        instance,
			                        session,
			                        full_body,
			                        hips);
	}

	if (not generic_trackers.empty())
		return xr::body_tracker(std::in_place_type_t<xr::htc_body_tracker>(),
		                        session,
		                        generic_trackers);

	if (instance.has_extension(XR_BD_BODY_TRACKING_EXTENSION_NAME))
	{
		auto bd_body_properties = system.bd_body_tracking_properties();
		if (bd_body_properties.supportsBodyTracking)
			return xr::body_tracker(std::in_place_type_t<xr::pico_body_tracker>(),
			                        instance,
			                        session);
	}

	return std::monostate();
}
