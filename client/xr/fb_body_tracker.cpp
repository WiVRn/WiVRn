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

#include "fb_body_tracker.h"
#include "spdlog/spdlog.h"
#include "xr/xr.h"
#include <openxr/openxr.h>

static PFN_xrDestroyBodyTrackerFB xrDestroyBodyTrackerFB{};

XrResult xr::destroy_fb_body_tracker(XrBodyTrackerFB id)
{
    return xrDestroyBodyTrackerFB(id);
}

xr::fb_body_tracker::fb_body_tracker(instance & inst, XrBodyTrackerFB h)
{
    id = h;
    xrLocateBodyJointsFB = inst.get_proc<PFN_xrLocateBodyJointsFB>("xrLocateBodyJointsFB");
}

void xr::fb_body_tracker::locate_spaces(XrTime time, std::vector<wivrn::from_headset::tracking::pose> & out_poses, XrSpace reference)
{
    assert(xrLocateBodyJointsFB);

    XrBodyJointsLocateInfoFB locate_info{
            .type = XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB,
            .next = nullptr,
            .baseSpace = reference,
            .time = time,
    };

    std::array<XrBodyJointLocationFB, XR_BODY_JOINT_COUNT_FB> joints{};
    XrBodyJointLocationsFB joint_locations{
            .type = XR_TYPE_BODY_JOINT_LOCATIONS_FB,
            .next = nullptr,
            .jointCount = joints.size(),
            .jointLocations = joints.data(),
    };

    if (auto res = xrLocateBodyJointsFB(id, &locate_info, &joint_locations); !XR_SUCCEEDED(res))
    {
        spdlog::warn("xrLocateBodyJointsFB returned {}", xr::to_string(res));

        // fill in poses
        for (size_t i = 0; i < joint_whitelist.size(); ++i)
        {
            wivrn::from_headset::tracking::pose pose{
                .pose = {},
                .device = wivrn::device_id::GENERIC_TRACKER,
                .flags = 0,
            };
            out_poses.push_back(std::move(pose));
        }
        return;
    }

    for (auto joint : joint_whitelist)
    {
        auto joint_pose = joints[joint];
        wivrn::from_headset::tracking::pose pose{
            .pose = joint_pose.pose,
            .device = wivrn::device_id::GENERIC_TRACKER,
            .flags = 0,
        };

        if (joint_pose.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
            pose.flags |= wivrn::from_headset::tracking::position_valid;
        if (joint_pose.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            pose.flags |= wivrn::from_headset::tracking::orientation_valid;
        if (joint_pose.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
            pose.flags |= wivrn::from_headset::tracking::position_tracked;
        if (joint_pose.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
            pose.flags |= wivrn::from_headset::tracking::orientation_tracked;

        out_poses.push_back(std::move(pose));
    }
    return;
}
