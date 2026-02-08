/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_meta_body_tracker.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "wivrn_session.h"
#include "xrt_cast.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_results.h"

static xrt_space_relation_flags cast_flags(uint8_t in_flags)
{
	std::underlying_type_t<xrt_space_relation_flags> flags = 0;
	if (in_flags & wivrn::from_headset::meta_body::position_valid)
		flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (in_flags & wivrn::from_headset::meta_body::orientation_valid)
		flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	if (in_flags & wivrn::from_headset::meta_body::position_tracked)
		flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	if (in_flags & wivrn::from_headset::meta_body::orientation_tracked)
		flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	return xrt_space_relation_flags(flags);
}

static xrt_space_relation to_relation(const wivrn::from_headset::meta_body::pose & pose)
{
	return {
	        .relation_flags = cast_flags(pose.flags),
	        .pose = xrt_cast(XrPosef{
	                .orientation = pose.orientation,
	                .position = pose.position,
	        }),
	};
}

namespace wivrn
{

xrt_full_body_joint_set_meta meta_body_joints_list::interpolate(const xrt_full_body_joint_set_meta & a, const xrt_full_body_joint_set_meta & b, float t)
{
	if (not a.base.is_active)
	{
		// in case neither is valid, both will be zeroed,
		// so return the later one for timestamp's sake
		return b;
	}
	else if (not b.base.is_active)
	{
		return a;
	}

	xrt_full_body_joint_set_meta result = b;

	for (size_t i = 0; i < std::size(result.joint_locations); i++)
	{
		result.joint_locations[i] = {
		        .relation = pose_list::interpolate(a.joint_locations[i].relation, b.joint_locations[i].relation, t),
		};
	}
	return result;
}

xrt_full_body_joint_set_meta meta_body_joints_list::extrapolate(const xrt_full_body_joint_set_meta & a, const xrt_full_body_joint_set_meta & b, int64_t ta, int64_t tb, int64_t t)
{
	if (not a.base.is_active)
	{
		// in case neither is valid, both will be zeroed,
		// so return the later one for timestamp's sake
		return b;
	}
	else if (not b.base.is_active)
	{
		return a;
	}

	xrt_full_body_joint_set_meta result = b;

	for (size_t i = 0; i < std::size(result.joint_locations); i++)
	{
		result.joint_locations[i] = {
		        .relation = pose_list::extrapolate(a.joint_locations[i].relation, b.joint_locations[i].relation, ta, tb, t),
		};
	}
	return result;
}

void meta_body_joints_list::update_tracking(const wivrn::from_headset::meta_body & tracking, const clock_offset & offset)
{
	xrt_full_body_joint_set_meta s{
	        .base = {
	                .sample_time_ns = tracking.timestamp,
	                .confidence = tracking.confidence,
	                .is_active = tracking.joints.has_value(),
	        },
	};
	if (s.base.is_active)
	{
		for (size_t joint = 0; joint < XRT_FULL_BODY_JOINT_COUNT_META; joint++)
		{
			// skip hands
			if (joint >= XRT_FULL_BODY_JOINT_LEFT_HAND_PALM_META and joint <= XRT_FULL_BODY_JOINT_RIGHT_HAND_LITTLE_TIP_META)
				continue;

			// offset the index into the packet's joints array, since we don't send hands
			size_t index = joint >= XRT_FULL_BODY_JOINT_LEFT_UPPER_LEG_META
			                       ? (joint - (XRT_FULL_BODY_JOINT_LEFT_UPPER_LEG_META - XRT_FULL_BODY_JOINT_LEFT_HAND_PALM_META))
			                       : joint;
			const auto & pose = (*tracking.joints)[index];
			s.joint_locations[joint].relation = to_relation(pose);
		}
	}
	add_sample(tracking.production_timestamp, tracking.timestamp, s, offset);
}

wivrn_meta_body_tracker::wivrn_meta_body_tracker(xrt_device * hmd, wivrn::wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_FB_BODY_TRACKING,
                .device_type = XRT_DEVICE_TYPE_BODY_TRACKER,
                .str = "WiVRn Meta Body Tracker",
                .serial = "WiVRn Meta Body Tracker",
                .tracking_origin = hmd->tracking_origin,
                .supported = {
                        .body_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_meta_body_tracker::update_inputs>,
                .get_body_joints = method_pointer<&wivrn_meta_body_tracker::get_body_joints>,
                .destroy = [](xrt_device *) {},
        },
        inputs_array{{
                {
                        .active = true,
                        .name = XRT_INPUT_FB_BODY_TRACKING,
                },
                {
                        .active = true,
                        .name = XRT_INPUT_META_FULL_BODY_TRACKING,
                },
        }},
        cnx(cnx)
{
	inputs = inputs_array.data();
	input_count = inputs_array.size();
}

xrt_result_t wivrn_meta_body_tracker::update_inputs()
{
	return XRT_SUCCESS;
}
xrt_result_t wivrn_meta_body_tracker::get_body_joints(xrt_input_name body_tracking_type, int64_t at_timestamp_ns, xrt_body_joint_set * out_value)
{
	if (body_tracking_type != XRT_INPUT_META_FULL_BODY_TRACKING)
	{
		U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), body_tracking_type);
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	XrTime production_timestamp;
	std::tie(production_timestamp, out_value->full_body_joint_set_meta) = joints_list.get_at(at_timestamp_ns);
	out_value->body_pose = xrt_space_relation{
	        .relation_flags = xrt_space_relation_flags(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                   XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT),
	        .pose = XRT_POSE_IDENTITY,
	};
	cnx.add_tracking_request(device_id::BODY, at_timestamp_ns, production_timestamp);
	return XRT_SUCCESS;
}

void wivrn_meta_body_tracker::update_tracking(const from_headset::meta_body & tracking, const clock_offset & offset)
{
	joints_list.update_tracking(tracking, offset);
}

} // namespace wivrn