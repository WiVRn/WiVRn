/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2024  galister <galister@librevr.org>
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

#include "wivrn_eye_tracker.h"

#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "util/u_logging.h"

#include <cmath>
#include <cstdint>
#include <openxr/openxr.h>

namespace wivrn
{

static void wivrn_eye_tracker_destroy(xrt_device * xdev);

static void wivrn_eye_tracker_update_inputs(xrt_device * xdev);

static void wivrn_eye_tracker_get_tracked_pose(xrt_device * xdev,
                                               xrt_input_name name,
                                               int64_t at_timestamp_ns,
                                               xrt_space_relation * out_relation);

wivrn_eye_tracker::wivrn_eye_tracker(xrt_device * hmd,
                                     std::shared_ptr<wivrn::wivrn_session> cnx) :
        xrt_device{}, gaze(device_id::EYE_GAZE), cnx(cnx)
{
	xrt_device * base = this;
	base->tracking_origin = hmd->tracking_origin;

	base->update_inputs = wivrn_eye_tracker_update_inputs;
	base->get_tracked_pose = wivrn_eye_tracker_get_tracked_pose;
	base->destroy = wivrn_eye_tracker_destroy;
	name = XRT_DEVICE_EYE_GAZE_INTERACTION;
	device_type = XRT_DEVICE_TYPE_EYE_TRACKER;
	eye_gaze_supported = true;

	// Print name.
	strcpy(str, "WiVRn Eye Tracker");
	strcpy(serial, "WiVRn Eye Tracker");

	gaze_input.active = true;
	gaze_input.name = XRT_INPUT_GENERIC_EYE_GAZE_POSE;

	// Setup input.
	inputs = &gaze_input;
	input_count = 1;
}

void wivrn_eye_tracker::update_inputs()
{
	// Empty
}

xrt_space_relation wivrn_eye_tracker::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns)
{
	if (name == XRT_INPUT_GENERIC_EYE_GAZE_POSE)
	{
		auto [_, relation] = gaze.get_at(at_timestamp_ns);
		return relation;
	}

	U_LOG_E("Unknown input name");
	return {};
}

void wivrn_eye_tracker::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	gaze.update_tracking(tracking, offset);
}

/*
 *
 * Functions
 *
 */

static void wivrn_eye_tracker_destroy(xrt_device * xdev)
{
	static_cast<wivrn_eye_tracker *>(xdev)->unregister();
}

static void wivrn_eye_tracker_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_eye_tracker *>(xdev)->update_inputs();
}

static void wivrn_eye_tracker_get_tracked_pose(xrt_device * xdev,
                                               xrt_input_name name,
                                               int64_t at_timestamp_ns,
                                               xrt_space_relation * out_relation)
{
	*out_relation = static_cast<wivrn_eye_tracker *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
}
} // namespace wivrn
