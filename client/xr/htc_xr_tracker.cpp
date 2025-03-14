/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Awzri <awzri@awzricat.com>
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

#include "htc_xr_tracker.h"
#include "space.h"
#include "wivrn_packets.h"
#include "xr/details/enumerate.h"
#include "xr/instance.h"
#include "xr/session.h"
#include <spdlog/spdlog.h>
#include <openxr/openxr.h>

// Note: This utilizes extensions not present in the OpenXR spec!
// For more info see:
// https://hub.vive.com/apidoc/api/VIVE.OpenXR.Tracker.ViveXRTracker.html
// https://hub.vive.com/apidoc/api/VIVE.OpenXR.VivePathEnumerationHelper.xrEnumeratePathsForInteractionProfileHTCDelegate.html

std::vector<xr::vive_xr_tracker> xr::vive_xr_trackers;

xr::vive_xr_tracker::vive_xr_tracker(uint8_t id, xr::space s)
{
	tracker_id = id;
	std::swap(s, space);
	is_active = false;
}

uint8_t xr::vive_xr_tracker::get_id()
{
	return tracker_id;
}

XrPosef xr::vive_xr_tracker::get_pose(instance & inst, session & session, XrTime time, XrSpace & reference)
{
	XrSpaceLocation location{.type = XR_TYPE_SPACE_LOCATION};
	xrLocateSpace(space, reference, time, &location);
	return location.pose;
};

xr::space * xr::vive_xr_tracker::get_space()
{
	return &space;
};

void xr::vive_xr_tracker::set_active(instance & inst, session & session)
{
	// Any tracker with an _ in its role name is probably inactive.
	if (xr_tracker_get_roles(inst, session)[tracker_id].contains("_"))
		is_active = false;
	else
		is_active = true;
}

bool xr::vive_xr_tracker::get_active()
{
	return is_active;
}

// Obtains a vector of user paths from the VUT interaction profile.
// Passing a user_path will give input paths for that specific user path
std::vector<XrPath> xr::xr_tracker_get_paths(instance & inst, XrPath user_path)
{
	xr::PFN_xrEnumeratePathsForInteractionProfileHTC xrEnumeratePathsForInteractionProfileHTC = inst.get_proc<xr::PFN_xrEnumeratePathsForInteractionProfileHTC>("xrEnumeratePathsForInteractionProfileHTC");

	if (!xrEnumeratePathsForInteractionProfileHTC)
		return {};

	XrPath tracker_profile = inst.string_to_path("/interaction_profiles/htc/vive_xr_tracker");

	// Yes, this is the structure type VIVE themselves gives it
	// https://github.com/ViveSoftware/VIVE-OpenXR-Unity/blob/25a5fd212420688952ead9deba735357656278ec/com.htc.upm.vive.openxr/Runtime/Features/PathEnumerate/Scripts/VivePathEnumeration.cs#L211
	// TODO wait for when vive makes a proper type and insert it when released
	XrPathsForInteractionProfileEnumerateInfoHTC enum_info{
	        .type = XR_TYPE_UNKNOWN,
	        .next = nullptr,
	        .interactionProfile = tracker_profile,
	        .userPath = user_path};

	return xr::details::enumerate<XrPath>(xrEnumeratePathsForInteractionProfileHTC, inst, enum_info);
}

std::vector<std::string> xr::xr_tracker_get_roles(instance & inst, session & session)
{
	std::vector<std::string> tracker_roles;

	for (auto & path: xr_tracker_get_paths(inst))
	{
		auto name_info = XrInputSourceLocalizedNameGetInfo{
		        .type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO,
		        .next = nullptr,
		        .sourcePath = path,
		        .whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT};

		tracker_roles.emplace_back(xr::details::enumerate<char>(xrGetInputSourceLocalizedName, session, &name_info));
	}

	return tracker_roles;
}

// Fill the packet with poses
std::vector<wivrn::from_headset::tracking::motion_tracker> xr::xr_tracker_compose_packet(
        instance & inst,
        session & session,
        XrTime time,
        XrSpace & reference,
        std::vector<xr::vive_xr_tracker> & trackers)
{
	std::vector<wivrn::from_headset::tracking::motion_tracker> packet;
	for (auto & tracker: trackers)
	{
		if (tracker.get_active())
		{
			packet.emplace_back(wivrn::from_headset::tracking::motion_tracker{.tracker_id = tracker.get_id(), .pose = tracker.get_pose(inst, session, time, reference)});
		}
	}
	return packet;
}
