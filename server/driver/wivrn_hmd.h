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

	xrt_input pose_input{
	        .active = true,
	        .name = XRT_INPUT_GENERIC_HEAD_POSE,
	};
	xrt_hmd_parts hmd_parts{};
	xrt_tracking_origin tracking_origin{
	        .name = "WiVRn origin",
	        .type = XRT_TRACKING_TYPE_OTHER,
	        .initial_offset = {
	                .orientation = {0, 0, 0, 1},
	        },
	};

	view_list views;
	from_headset::battery battery{};

	std::atomic<bool> presence{true};
	// last XR_EVENT_DATA_USER_PRESENCE_CHANGED_EXT from headset
	// we must keep track of this to not go out of sync with headset when
	// a session state change also triggers presence change
	std::atomic<bool> real_presence{true};
	thread_safe<std::array<std::optional<from_headset::visibility_mask_changed::masks>, 2>> visibility_mask;

	wivrn::wivrn_session * cnx;

	xrt_result_t get_visibility_mask(xrt_visibility_mask_type, uint32_t view_index, xrt_visibility_mask **);

public:
	using base = xrt_device;
	wivrn_hmd(wivrn::wivrn_session * cnx,
	          const from_headset::headset_info_packet & info);

	void set_foveated_size(uint32_t width, uint32_t height);

	xrt_result_t get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation *);
	xrt_result_t get_presence(bool * out_presence);
	xrt_result_t get_view_poses(const xrt_vec3 * default_eye_relation,
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
	void update_visibility_mask(const from_headset::visibility_mask_changed &);
	// real if this update comes from a presence changed event
	bool update_presence(bool new_presence, bool real);
};
} // namespace wivrn
