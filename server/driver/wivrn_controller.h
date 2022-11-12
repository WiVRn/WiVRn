// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "xrt/xrt_device.h"

#include "pose_list.h"
#include "wivrn_session.h"

#include <mutex>
#include <vector>
#include <memory>

class wivrn_controller : public xrt_device
{
	std::mutex mutex;

	pose_list grip;
	pose_list aim;

	std::vector<xrt_input> inputs_staging;
	std::vector<xrt_input> inputs_array;
	xrt_output haptic_output;

	std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx;

public:
	wivrn_controller(int hand_id, xrt_device *hmd, std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx);

	void
	unregister()
	{
		cnx = nullptr;
	}
	void
	update_inputs();

	xrt_space_relation
	get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns);

	void
	set_output(xrt_output_name name, const xrt_output_value *value);

	void
	set_inputs(const from_headset::inputs &);

	void
	update_tracking(const from_headset::tracking &, const clock_offset &);

private:
	void
	set_inputs(device_id input_id, float value);
};
