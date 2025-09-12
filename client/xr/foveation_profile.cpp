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

#include "foveation_profile.h"
#include "xr/instance.h"
#include "xr/session.h"

xr::foveation_profile::foveation_profile(instance & inst, session & s, XrFoveationLevelFB level, float vertical_offset_degrees, bool dynamic) :
        handle(inst.get_proc<PFN_xrDestroyFoveationProfileFB>("xrDestroyFoveationProfileFB")),
        level_(level),
        vertical_offset_degrees_(vertical_offset_degrees),
        dynamic_(dynamic)
{
	auto create = inst.get_proc<PFN_xrCreateFoveationProfileFB>("xrCreateFoveationProfileFB");
	XrFoveationLevelProfileCreateInfoFB level_info{
	        .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
	        .level = level,
	        .verticalOffset = vertical_offset_degrees,
	        .dynamic = dynamic ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB : XR_FOVEATION_DYNAMIC_DISABLED_FB,
	};
	XrFoveationProfileCreateInfoFB info{
	        .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
	        .next = &level_info,
	};
	create(s, &info, &id);
}
