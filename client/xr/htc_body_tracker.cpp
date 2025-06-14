/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Awzri <awzri@awzricat.com>
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

#include "htc_body_tracker.h"
#include "xr/details/enumerate.h"
#include "xr/htc_exts.h"
#include "xr/xr.h"
#include <openxr/openxr.h>

// Note: This utilizes extensions not present in the OpenXR spec!
// For more info see:
// https://hub.vive.com/apidoc/api/VIVE.OpenXR.Tracker.ViveXRTracker.html
// https://hub.vive.com/apidoc/api/VIVE.OpenXR.VivePathEnumerationHelper.xrEnumeratePathsForInteractionProfileHTCDelegate.html

xr::vive_xr_tracker::vive_xr_tracker(xr::space && space) :
        space(std::move(space)) {}
void xr::vive_xr_tracker::set_active(bool active)
{
	is_active = active;
}
bool xr::vive_xr_tracker::get_active() const
{
	return is_active;
}
XrSpace xr::vive_xr_tracker::get_space() const
{
	return space;
}

static wivrn::from_headset::body_tracking::pose locate_space(XrSpace space, XrSpace reference, XrTime time)
{
	XrSpaceVelocity velocity{
	        .type = XR_TYPE_SPACE_VELOCITY,
	};

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	        .next = &velocity,
	};

	xrLocateSpace(space, reference, time, &location);

	wivrn::from_headset::body_tracking::pose res{
	        .pose = location.pose,
	        .flags = 0,
	};

	if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
		res.flags |= wivrn::from_headset::body_tracking::orientation_valid;

	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		res.flags |= wivrn::from_headset::body_tracking::position_valid;

	if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
		res.flags |= wivrn::from_headset::body_tracking::orientation_tracked;

	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
		res.flags |= wivrn::from_headset::body_tracking::position_tracked;

	return res;
}

std::vector<std::string> xr::htc_body_tracker::get_roles()
{
	auto paths = get_paths();
	std::vector<std::string> roles{};
	roles.reserve(paths.size());

	for (auto & path: paths)
	{
		XrInputSourceLocalizedNameGetInfo get_info{
		        .type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO,
		        .next = nullptr,
		        .sourcePath = path,
		        .whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT,
		};

		roles.emplace_back(xr::details::enumerate<char>(xrGetInputSourceLocalizedName, s, &get_info));
	}

	return roles;
}

xr::htc_body_tracker::htc_body_tracker(instance & inst, session & s) :
        inst(&inst),
        s(s)
{
	xrEnumeratePathsForInteractionProfileHTC = inst.get_proc<PFN_xrEnumeratePathsForInteractionProfileHTC>("xrEnumeratePathsForInteractionProfileHTC");
}

std::vector<XrPath> xr::htc_body_tracker::get_paths(XrPath user_path)
{
	assert(xrEnumeratePathsForInteractionProfileHTC);
	XrPath tracker_profile = inst->string_to_path("/interaction_profiles/htc/vive_xr_tracker");

	XrPathsForInteractionProfileEnumerateInfoHTC profile_info{
	        .type = XR_TYPE_UNKNOWN,
	        .next = nullptr,
	        .interactionProfile = tracker_profile,
	        .userPath = user_path,
	};
	return xr::details::enumerate<XrPath>(xrEnumeratePathsForInteractionProfileHTC, *inst, profile_info);
}

void xr::htc_body_tracker::add(xr::space && space)
{
	trackers.emplace_back(std::move(space));
}
size_t xr::htc_body_tracker::count() const
{
	return trackers.size();
}

void xr::htc_body_tracker::update_active()
{
	auto roles = get_roles();
	assert(roles.size() >= trackers.size());
	for (int i = 0; i < trackers.size(); i++)
		trackers[i].set_active(!roles[i].contains('_'));
}

std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses> xr::htc_body_tracker::locate_spaces(XrTime time, XrSpace reference)
{
	std::array<wivrn::from_headset::body_tracking::pose, wivrn::from_headset::body_tracking::max_tracked_poses> poses{};

	for (int i = 0; i < trackers.size(); i++)
	{
		auto & tracker = trackers.at(i);
		wivrn::from_headset::body_tracking::pose pose{
		        .pose = {},
		        .flags = 0,
		};
		if (tracker.get_active())
			pose = locate_space(tracker.get_space(), reference, time);

		poses[i] = pose;
	}

	return poses;
}
