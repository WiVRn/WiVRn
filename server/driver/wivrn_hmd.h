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

#pragma once

#include "utils/thread_safe.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include "view_list.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace wivrn
{
class wivrn_session;

class wivrn_hmd : public xrt_device
{
	std::mutex mutex;

	xrt_input pose_input{};
	xrt_hmd_parts hmd_parts{};
	xrt_tracking_origin tracking_origin{
	        .name = "WiVRn origin",
	        .type = XRT_TRACKING_TYPE_OTHER,
	        .initial_offset = {
	                .orientation = {0, 0, 0, 1},
	        },
	};

	view_list views;
	std::array<to_headset::foveation_parameter, 2> foveation_parameters{};
	from_headset::battery battery{};

	thread_safe<std::array<std::optional<from_headset::visibility_mask_changed::masks>, 2>> visibility_mask;

	wivrn::wivrn_session * cnx;

	static bool wivrn_hmd_compute_distortion(xrt_device * xdev, uint32_t view_index, float u, float v, xrt_uv_triplet * result);

	xrt_result_t get_visibility_mask(xrt_visibility_mask_type, uint32_t view_index, xrt_visibility_mask **);

public:
	using base = xrt_device;
	wivrn_hmd(wivrn::wivrn_session * cnx,
	          const from_headset::headset_info_packet & info);

	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation *);
	void get_view_poses(const xrt_vec3 * default_eye_relation,
	                    int64_t at_timestamp_ns,
	                    uint32_t view_count,
	                    xrt_space_relation * out_head_relation,
	                    xrt_fov * out_fovs,
	                    xrt_pose * out_poses);
	xrt_result_t get_battery_status(bool * out_present,
	                                bool * out_charging,
	                                float * out_charge);

	void update_battery(const from_headset::battery &);
	void update_tracking(const from_headset::tracking &, const clock_offset &);

	decltype(foveation_parameters) set_foveated_size(uint32_t width, uint32_t height);
	void set_foveation_center(std::array<xrt_vec2, 2> center);

	std::array<to_headset::foveation_parameter, 2> get_foveation_parameters()
	{
		return foveation_parameters;
	}

	void update_visibility_mask(const from_headset::visibility_mask_changed &);
};
} // namespace wivrn
