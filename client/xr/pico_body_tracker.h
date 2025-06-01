/*
 * WiVRn VR streaming
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

#pragma once

#include "utils/handle.h"
#include "wivrn_packets.h"

#include <openxr/openxr.h>
#include <vector>

namespace xr
{
class instance;
class session;

XrResult destroy_pico_body_tracker(XrBodyTrackerBD);

class pico_body_tracker : public utils::handle<XrBodyTrackerBD, destroy_pico_body_tracker>
{
    PFN_xrLocateBodyJointsBD xrLocateBodyJointsBD{};

public:
    pico_body_tracker() = default;
    pico_body_tracker(instance & inst, XrBodyTrackerBD h);

    void locate_spaces(XrTime time, std::vector<wivrn::from_headset::tracking::pose> & out_poses, XrSpace reference);
};
} // namespace xr
