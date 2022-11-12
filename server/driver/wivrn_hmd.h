// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include "view_list.h"
#include "wivrn_session.h"

#include <mutex>
#include <memory>

class wivrn_hmd : public xrt_device
{
	std::mutex mutex;

	float fps;

	xrt_input pose_input;
	xrt_hmd_parts hmd_parts;
	xrt_tracking_origin tracking_origin;

	view_list views;

	std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx;

public:
	wivrn_hmd(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx,
	          const from_headset::headset_info_packet &info);
	void
	unregister()
	{
		cnx = nullptr;
	}

	void
	update_inputs();
	xrt_space_relation
	get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns);
	comp_target *
	create_compositor_target(struct comp_compositor *comp);
	void
	get_view_poses(const xrt_vec3 *default_eye_relation,
	               uint64_t at_timestamp_ns,
	               uint32_t view_count,
	               xrt_space_relation *out_head_relation,
	               xrt_fov *out_fovs,
	               xrt_pose *out_poses);

	void
	update_tracking(const from_headset::tracking &, const clock_offset &);
};
