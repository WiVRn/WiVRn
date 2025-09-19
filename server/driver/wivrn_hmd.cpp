/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_hmd.h"
#include "wivrn_session.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "util/u_device.h"
#include "util/u_logging.h"
#include "utils/method.h"

#include "xrt_cast.h"
#include <cstdint>
#include <stdio.h>
#include <openxr/openxr.h>

#include "configuration.h"

namespace wivrn
{

xrt_result_t wivrn_hmd::get_visibility_mask(xrt_visibility_mask_type type, uint32_t view_index, xrt_visibility_mask ** mask)
{
	static_assert(sizeof(uint32_t) == sizeof(decltype(from_headset::visibility_mask_changed::mask::indices)::value_type));
	static_assert(sizeof(xrt_vec2) == sizeof(decltype(from_headset::visibility_mask_changed::mask::vertices)::value_type));
	const auto visibility_mask = this->visibility_mask.lock();
	if (type > from_headset::visibility_mask_changed::num_types or view_index >= 2 or not(*visibility_mask)[view_index])
	{
		*mask = (xrt_visibility_mask *)calloc(1, sizeof(xrt_visibility_mask));
		return XRT_SUCCESS;
	}
	const auto & in_mask = (*(*visibility_mask)[view_index])[int(type - 1)];
	size_t index_size = in_mask.indices.size() * sizeof(uint32_t);
	size_t vertex_size = in_mask.vertices.size() * sizeof(xrt_vec2);
	*mask = (xrt_visibility_mask *)calloc(1, sizeof(xrt_visibility_mask) + index_size + vertex_size);
	**mask = {
	        .type = type,
	        .index_count = uint32_t(in_mask.indices.size()),
	        .vertex_count = uint32_t(in_mask.vertices.size()),
	};
	memcpy(xrt_visibility_mask_get_indices(*mask), in_mask.indices.data(), index_size);
	memcpy(xrt_visibility_mask_get_vertices(*mask), in_mask.vertices.data(), vertex_size);
	return XRT_SUCCESS;
}

wivrn_hmd::wivrn_hmd(wivrn::wivrn_session * cnx,
                     const from_headset::headset_info_packet & info) :
        xrt_device{
                .name = XRT_DEVICE_GENERIC_HMD,
                .device_type = XRT_DEVICE_TYPE_HMD,
                .str = "WiVRn HMD",
                .serial = "WiVRn HMD",
                .hmd = &hmd_parts,
                .tracking_origin = &tracking_origin,
                .input_count = 1,
                .inputs = &pose_input,
                .supported = {
                        .orientation_tracking = true,
                        .position_tracking = true,
                        .presence = info.user_presence,
                        .battery_status = true,
                },
                .update_inputs = [](xrt_device *) { return XRT_SUCCESS; },
                .get_tracked_pose = method_pointer<&wivrn_hmd::get_tracked_pose>,
                .get_presence = method_pointer<&wivrn_hmd::get_presence>,
                .get_view_poses = method_pointer<&wivrn_hmd::get_view_poses>,
                .get_visibility_mask = method_pointer<&wivrn_hmd::get_visibility_mask>,
                .get_battery_status = method_pointer<&wivrn_hmd::get_battery_status>,
                .destroy = [](xrt_device *) {},
        },
        cnx(cnx)
{
	const auto config = configuration();

	auto eye_width = info.recommended_eye_width;
	eye_width = ((eye_width + 3) / 4) * 4;
	auto eye_height = info.recommended_eye_height;
	eye_height = ((eye_height + 3) / 4) * 4;

	// Setup info.
	hmd->view_count = 2;
	hmd->blend_modes[hmd->blend_mode_count++] = XRT_BLEND_MODE_OPAQUE;
	if (info.passthrough)
		hmd->blend_modes[hmd->blend_mode_count++] = XRT_BLEND_MODE_ALPHA_BLEND;

	hmd->distortion.models = XRT_DISTORTION_MODEL_NONE;
	hmd->distortion.preferred = XRT_DISTORTION_MODEL_NONE;
	hmd->screens[0].w_pixels = eye_width * 2;
	hmd->screens[0].h_pixels = eye_height;

	// Left
	hmd->views[0].display.w_pixels = eye_width;
	hmd->views[0].display.h_pixels = eye_height;
	hmd->views[0].rot = u_device_rotation_ident;

	// Right
	hmd->views[1].display.w_pixels = eye_width;
	hmd->views[1].display.h_pixels = eye_height;
	hmd->views[1].rot = u_device_rotation_ident;

	// FOV from headset info packet
	hmd->distortion.fov[0] = xrt_cast(info.fov[0]);
	hmd->distortion.fov[1] = xrt_cast(info.fov[1]);
}

xrt_result_t wivrn_hmd::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * res)
{
	if (name != XRT_INPUT_GENERIC_HEAD_POSE)
	{
		U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
		return XRT_ERROR_INPUT_UNSUPPORTED;
	}

	auto [extrapolation_time, view] = views.get_at(at_timestamp_ns);
	*res = view.relation;
	cnx->add_predict_offset(extrapolation_time);
	return XRT_SUCCESS;
}

void wivrn_hmd::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	views.update_tracking(tracking, offset);
}

void wivrn_hmd::update_battery(const from_headset::battery & new_battery)
{
	// We will only request a new sample if the current one is consumed
	cnx->set_enabled(to_headset::tracking_control::id::battery, false);
	std::lock_guard lock(mutex);
	battery = new_battery;
}

xrt_result_t wivrn_hmd::get_presence(bool * out_presence)
{
	*out_presence = presence;

	return XRT_SUCCESS;
}

xrt_result_t wivrn_hmd::get_view_poses(const xrt_vec3 * default_eye_relation,
                                       int64_t at_timestamp_ns,
                                       uint32_t view_count,
                                       xrt_space_relation * out_head_relation,
                                       xrt_fov * out_fovs,
                                       xrt_pose * out_poses)
{
	auto [extrapolation_time, view] = views.get_at(at_timestamp_ns);
	cnx->add_predict_offset(extrapolation_time);

	int flags = view.relation.relation_flags;

	if (not(view.flags & XR_VIEW_STATE_POSITION_VALID_BIT))
		flags &= ~XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (not(view.flags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
		flags &= ~XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	if (not(view.flags & XR_VIEW_STATE_POSITION_TRACKED_BIT))
		flags &= ~XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	if (not(view.flags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT))
		flags &= ~XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;

	view.relation.relation_flags = (xrt_space_relation_flags)flags;
	*out_head_relation = view.relation;

	assert(view_count == 2);
	for (size_t eye = 0; eye < 2; ++eye)
	{
		out_fovs[eye] = view.fovs[eye];
		out_poses[eye] = view.poses[eye];
	}
	return XRT_SUCCESS;
}

xrt_result_t wivrn_hmd::get_battery_status(bool * out_present,
                                           bool * out_charging,
                                           float * out_charge)
{
	cnx->set_enabled(to_headset::tracking_control::id::battery, true);

	std::lock_guard lock(mutex);
	*out_present = battery.present;
	*out_charging = battery.charging;
	*out_charge = battery.charge;

	return XRT_SUCCESS;
}

void wivrn_hmd::set_foveated_size(uint32_t width, uint32_t height)
{
	assert(width % 2 == 0);
	uint32_t eye_width = width / 2;

	hmd->screens[0].w_pixels = width;
	hmd->screens[0].h_pixels = height;

	for (int i = 0; i < 2; ++i)
	{
		auto & view = hmd->views[i];
		view.viewport.x_pixels = i * eye_width;
		view.viewport.y_pixels = 0;

		view.viewport.w_pixels = eye_width;
		view.viewport.h_pixels = height;
	}
}

void wivrn_hmd::update_visibility_mask(const from_headset::visibility_mask_changed & mask)
{
	assert(mask.view_index < 2);
	auto m = visibility_mask.lock();
	m->at(mask.view_index) = mask.data;
}

bool wivrn_hmd::update_presence(bool new_presence)
{
	if (this->presence.exchange(new_presence) != new_presence)
	{
		U_LOG_I("user presence changed to %s", new_presence ? "true" : "false");
		return true;
	}
	return false;
}
} // namespace wivrn
