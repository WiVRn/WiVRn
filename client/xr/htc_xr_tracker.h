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

#pragma once

#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/space.h"
#include <cstdint>
#include <vector>
#include <openxr/openxr.h>

namespace xr
{
typedef struct XrPathsForInteractionProfileEnumerateInfoHTC
{
	XrStructureType type;
	const void * XR_MAY_ALIAS next;
	XrPath interactionProfile;
	XrPath userPath;
} XrPathsForInteractionProfileEnumerateInfoHTC;

class vive_xr_tracker
{
	uint8_t tracker_id;
	bool is_active;
	xr::space space;

public:
	vive_xr_tracker(uint8_t id, xr::space s);
	uint8_t get_id();
	XrPosef get_pose(instance & inst, session & session, XrTime time, XrSpace & reference);
	void set_active(instance & inst, session & session);
	bool get_active();
	xr::space * get_space();
};

extern std::vector<xr::vive_xr_tracker> vive_xr_trackers;

typedef XrResult(XRAPI_PTR * PFN_xrEnumeratePathsForInteractionProfileHTC)(XrInstance instance, XrPathsForInteractionProfileEnumerateInfoHTC createInfo, uint32_t pathCapacityInput, uint32_t * pathCountOutput, XrPath * paths);
extern PFN_xrEnumeratePathsForInteractionProfileHTC xrEnumeratePathsForInteractionProfileHTC;

std::vector<XrPath> xr_tracker_get_paths(instance & inst, XrPath user_path = XR_NULL_PATH);
std::vector<std::string> xr_tracker_get_roles(instance & inst, session & session);
std::vector<wivrn::from_headset::tracking::motion_tracker> xr_tracker_compose_packet(instance & inst, session & session, XrTime time, XrSpace & reference, std::vector<xr::vive_xr_tracker> & trackers);
} // namespace xr
