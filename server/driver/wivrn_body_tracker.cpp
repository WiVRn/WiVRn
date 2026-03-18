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

#include "wivrn_body_tracker.h"
#include "driver/pose_list.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "utils/overloaded.h"
#include "wivrn_packets.h"
#include "wivrn_session.h"
#include "xrt_cast.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_results.h"

static_assert(std::to_underlying(XR_BODY_JOINT_COUNT_FB) == std::to_underlying(XRT_BODY_JOINT_COUNT_FB));
static_assert(std::to_underlying(XR_FULL_BODY_JOINT_COUNT_META) == std::to_underlying(XRT_FULL_BODY_JOINT_COUNT_META));
static_assert(XR_BODY_JOINT_COUNT_BD == XRT_BODY_JOINT_COUNT_BD);

static xrt_space_relation to_relation(const wivrn::from_headset::meta_body::pose & pose)
{
	return {
	        .relation_flags = from_pose_flags(pose.flags),
	        .pose = xrt_cast(XrPosef{
	                .orientation = pose.orientation,
	                .position = pose.position,
	        }),
	};
}
static xrt_space_relation to_relation(const wivrn::from_headset::bd_body::pose & pose)
{
	return {
	        .relation_flags = from_pose_flags(pose.flags),
	        .pose = xrt_cast(XrPosef{
	                .orientation = pose.orientation,
	                .position = pose.position,
	        }),
	};
}
static xrt_space_relation to_relation(const wivrn::from_headset::meta_body::pose & base, const wivrn::from_headset::meta_body::packed_pose & pose)
{
	return {
	        .relation_flags = from_pose_flags(pose.flags),
	        .pose = xrt_cast(XrPosef{
	                .orientation = pose.orientation,
	                .position = {
	                        .x = base.position.x + float(pose.position.x / 10'000.f),
	                        .y = base.position.y + float(pose.position.y / 10'000.f),
	                        .z = base.position.z + float(pose.position.z / 10'000.f),
	                },
	        }),
	};
}

static inline bool is_joint_set_active(wivrn::from_headset::body_type body_type, const xrt_body_joint_set & set)
{
	switch (body_type)
	{
		case wivrn::from_headset::body_type::fb:
		case wivrn::from_headset::body_type::meta:
			return set.base_body_joint_set_meta.is_active;
		case wivrn::from_headset::body_type::bd:
			return set.body_joint_set_bd.is_active;
		default:
			assert(false);
			__builtin_unreachable();
	}
}

template <typename T>
static void interpolate_joints(const T & a, const T & b, T & out, float t)
{
	for (auto [a, b, out]: std::views::zip(a.joint_locations, b.joint_locations, out.joint_locations))
		out.relation = wivrn::pose_list::interpolate(a.relation, b.relation, t);
}

template <typename T>
static void extrapolate_joints(const T & a, const T & b, T & out, int64_t ta, int64_t tb, int64_t t)
{
	for (auto [a, b, out]: std::views::zip(a.joint_locations, b.joint_locations, out.joint_locations))
		out.relation = wivrn::pose_list::extrapolate(a.relation, b.relation, ta, tb, t);
}

namespace wivrn
{
xrt_body_joint_set body_joints_list::interpolate(const xrt_body_joint_set & a, const xrt_body_joint_set & b, float t)
{
	if (not is_joint_set_active(type, a))
	{
		// in case neither is valid, both will be zeroed,
		// so return the later one for timestamp's sake
		return b;
	}
	else if (not is_joint_set_active(type, b))
	{
		return a;
	}

	xrt_body_joint_set result = b;
	switch (type)
	{
		case wivrn::from_headset::body_type::fb:
			interpolate_joints(a.body_joint_set_fb, b.body_joint_set_fb, result.body_joint_set_fb, t);
			break;
		case wivrn::from_headset::body_type::meta:
			interpolate_joints(a.full_body_joint_set_meta, b.full_body_joint_set_meta, result.full_body_joint_set_meta, t);
			break;
		case wivrn::from_headset::body_type::bd:
			interpolate_joints(a.body_joint_set_bd, b.body_joint_set_bd, result.body_joint_set_bd, t);
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}
	return result;
}

xrt_body_joint_set body_joints_list::extrapolate(const xrt_body_joint_set & a, const xrt_body_joint_set & b, int64_t ta, int64_t tb, int64_t t)
{
	if (not is_joint_set_active(type, a))
	{
		// in case neither is valid, both will be zeroed,
		// so return the later one for timestamp's sake
		return b;
	}
	else if (not is_joint_set_active(type, b))
	{
		return a;
	}

	xrt_body_joint_set result = b;
	switch (type)
	{
		case wivrn::from_headset::body_type::fb:
			extrapolate_joints(a.body_joint_set_fb, b.body_joint_set_fb, result.body_joint_set_fb, ta, tb, t);
			break;
		case wivrn::from_headset::body_type::meta:
			extrapolate_joints(a.full_body_joint_set_meta, b.full_body_joint_set_meta, result.full_body_joint_set_meta, ta, tb, t);
			break;
		case wivrn::from_headset::body_type::bd:
			extrapolate_joints(a.body_joint_set_bd, b.body_joint_set_bd, result.body_joint_set_bd, ta, tb, t);
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}
	return result;
}

void body_joints_list::update_tracking(const wivrn::from_headset::meta_body & tracking, const clock_offset & offset)
{
	assert(type == from_headset::body_type::fb or type == from_headset::body_type::meta);

	xrt_body_joint_set s{
	        .base_body_joint_set_meta = {
	                .sample_time_ns = tracking.timestamp,
	                .confidence = tracking.confidence,
	                .is_active = not std::holds_alternative<std::monostate>(tracking.joints),
	        },
	};
	if (s.base_body_joint_set_meta.is_active)
	{
		std::visit(utils::overloaded{
		                   [](std::monostate) {
			                   __builtin_unreachable();
		                   },
		                   [&](auto & joints) {
			                   s.body_joint_set_fb.joint_locations[XRT_BODY_JOINT_ROOT_FB].relation = to_relation(joints.root);
			                   for (size_t joint = XRT_BODY_JOINT_HIPS_FB; joint < std::size(joints.joints) + 1; joint++)
			                   {
				                   static_assert(offsetof(xrt_full_body_joint_set_meta, joint_locations) == offsetof(xrt_body_joint_set_fb, joint_locations));
				                   static_assert(sizeof(xrt_full_body_joint_set_meta::joint_locations[0]) == sizeof(xrt_body_joint_set_fb::joint_locations[0]));
				                   s.full_body_joint_set_meta.joint_locations[joint].relation = to_relation(joints.root, joints.joints[joint - 1]);
			                   }
		                   }},
		           tracking.joints);
	}

	add_sample(tracking.production_timestamp, tracking.timestamp, s, offset);
}
void body_joints_list::update_tracking(const wivrn::from_headset::bd_body & tracking, const clock_offset & offset)
{
	assert(type == from_headset::body_type::bd);

	xrt_body_joint_set s{
	        .body_joint_set_bd = {
	                .sample_time_ns = tracking.timestamp,
	                .is_active = true, // FIXME: what is this supposed to be?
	                .all_joint_poses_tracked = tracking.all_tracked,
	        },
	};
	for (size_t joint = XRT_BODY_JOINT_PELVIS_BD; joint < XRT_BODY_JOINT_COUNT_BD; joint++)
	{
		s.body_joint_set_bd.joint_locations[joint].relation = to_relation(tracking.joints[joint]);
	}

	add_sample(tracking.production_timestamp, tracking.timestamp, s, offset);
}

wivrn_body_tracker::wivrn_body_tracker(xrt_device * hmd, wivrn::wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_FB_BODY_TRACKING, // FIXME: replace with something more appropriate, when there is one
                .device_type = XRT_DEVICE_TYPE_BODY_TRACKER,
                .tracking_origin = hmd->tracking_origin,
                .supported = {
                        .body_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_body_tracker::update_inputs>,
                .get_body_skeleton = method_pointer<&wivrn_body_tracker::get_body_skeleton>,
                .get_body_joints = method_pointer<&wivrn_body_tracker::get_body_joints>,
                .destroy = [](xrt_device *) {},
        },
        joints_list(cnx.get_info().body_tracking),
        cnx(cnx)
{
	switch (cnx.get_info().body_tracking)
	{
		case from_headset::body_type::meta:
			strcpy(str, "WiVRn Meta body tracker");
			strcpy(serial, "WiVRn Meta body tracker");
			inputs_array.push_back({
			        .active = true,
			        .name = XRT_INPUT_FB_BODY_TRACKING,
			});
			inputs_array.push_back({
			        .active = true,
			        .name = XRT_INPUT_META_FULL_BODY_TRACKING,
			});
			break;
		case from_headset::body_type::fb:
			strcpy(str, "WiVRn FB body tracker");
			strcpy(serial, "WiVRn FB body tracker");
			inputs_array.push_back({
			        .active = true,
			        .name = XRT_INPUT_FB_BODY_TRACKING,
			});
			break;
		case from_headset::body_type::bd:
			strcpy(str, "WiVRn BD body Tracker");
			strcpy(serial, "WiVRn BD body tracker");
			inputs_array.push_back({
			        .active = true,
			        .name = XRT_INPUT_BD_BODY_TRACKING,
			});
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}
	inputs = inputs_array.data();
	input_count = inputs_array.size();
}

xrt_result_t wivrn_body_tracker::update_inputs()
{
	return XRT_SUCCESS;
}
xrt_result_t wivrn_body_tracker::get_body_skeleton(xrt_input_name body_skeleton_type, xrt_body_skeleton * out_value)
{
	if (!std::ranges::contains(inputs_array, body_skeleton_type, &xrt_input::name))
	{
		U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), body_skeleton_type);
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	auto s = this->meta_skeleton.lock();
	*out_value = *s;
	return XRT_SUCCESS;
}
xrt_result_t wivrn_body_tracker::get_body_joints(xrt_input_name body_tracking_type, int64_t at_timestamp_ns, xrt_body_joint_set * out_value)
{
	if (!std::ranges::contains(inputs_array, body_tracking_type, &xrt_input::name))
	{
		U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), body_tracking_type);
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	XrTime production_timestamp;
	std::tie(production_timestamp, *out_value) = joints_list.get_at(at_timestamp_ns);
	out_value->body_pose = xrt_space_relation{
	        .relation_flags = xrt_space_relation_flags(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                   XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT),
	        .pose = XRT_POSE_IDENTITY,
	};

	if (body_tracking_type == XRT_INPUT_FB_BODY_TRACKING or body_tracking_type == XRT_INPUT_META_FULL_BODY_TRACKING)
		out_value->base_body_joint_set_meta.skeleton_changed_count = meta_skeleton_generation;

	cnx.add_tracking_request(device_id::BODY, at_timestamp_ns, production_timestamp);
	return XRT_SUCCESS;
}

void wivrn_body_tracker::update_tracking(const from_headset::meta_body & tracking, const clock_offset & offset)
{
	joints_list.update_tracking(tracking, offset);
}
void wivrn_body_tracker::update_tracking(const from_headset::bd_body & tracking, const clock_offset & offset)
{
	joints_list.update_tracking(tracking, offset);
}
void wivrn_body_tracker::update_skeleton(const from_headset::meta_body_skeleton & skeleton)
{
	assert(std::ranges::contains(inputs_array, XRT_INPUT_FB_BODY_TRACKING, &xrt_input::name));

	auto s = this->meta_skeleton.lock();
	std::visit(utils::overloaded{
	                   [&s](auto & skeleton) {
		                   for (size_t i = 0; i < skeleton.joints.size(); i++)
		                   {
			                   static_assert(offsetof(xrt_full_body_skeleton_meta, joints) == offsetof(xrt_body_skeleton_fb, joints));
			                   static_assert(sizeof(xrt_full_body_skeleton_meta::joints[0]) == sizeof(xrt_body_skeleton_fb::joints[0]));
			                   const auto & joint = skeleton.joints[i];
			                   s->full_body_skeleton_meta.joints[i] = xrt_body_skeleton_joint_fb{
			                           .pose = xrt_cast(joint.pose),
			                           .joint = joint.joint,
			                           .parent_joint = joint.parentJoint,
			                   };
		                   }
	                   }},
	           skeleton.skeleton);
	meta_skeleton_generation++;
}

} // namespace wivrn