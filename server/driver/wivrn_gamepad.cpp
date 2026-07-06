/*
 * WiVRn VR streaming
 * Copyright (C) 2026  JR Lanteigne <root@dnim.dev>
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

#include "wivrn_gamepad.h"

#include "wivrn_gamepad_map.h"
#include "wivrn_session.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "os/os_time.h"
#include "util/u_logging.h"
#include "utils/method.h"

namespace wivrn
{

static_assert(gp_count == 18);

wivrn_gamepad::wivrn_gamepad(wivrn::wivrn_session & cnx) :
        xrt_device{
                .name = XRT_DEVICE_XBOX_CONTROLLER,
                .device_type = XRT_DEVICE_TYPE_GAMEPAD,
                .str = "WiVRn Gamepad",
                .serial = "WiVRn Gamepad",
                .tracking_origin = &origin,
                .update_inputs = method_pointer<&wivrn_gamepad::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_gamepad::get_tracked_pose>,
                .set_output = method_pointer<&wivrn_gamepad::set_output>,
                .destroy = [](xrt_device *) {},
        },
        origin{
                .type = XRT_TRACKING_TYPE_OTHER,
                .initial_offset = XRT_POSE_IDENTITY,
        },
        cnx(&cnx)
{
	inputs_array = {};
	inputs_staging = {};
	inputs = inputs_array.data();
	input_count = inputs_array.size();

	inputs_array[gp_menu].name = XRT_INPUT_XBOX_MENU_CLICK;
	inputs_array[gp_view].name = XRT_INPUT_XBOX_VIEW_CLICK;
	inputs_array[gp_a].name = XRT_INPUT_XBOX_A_CLICK;
	inputs_array[gp_b].name = XRT_INPUT_XBOX_B_CLICK;
	inputs_array[gp_x].name = XRT_INPUT_XBOX_X_CLICK;
	inputs_array[gp_y].name = XRT_INPUT_XBOX_Y_CLICK;
	inputs_array[gp_dpad_down].name = XRT_INPUT_XBOX_DPAD_DOWN_CLICK;
	inputs_array[gp_dpad_right].name = XRT_INPUT_XBOX_DPAD_RIGHT_CLICK;
	inputs_array[gp_dpad_up].name = XRT_INPUT_XBOX_DPAD_UP_CLICK;
	inputs_array[gp_dpad_left].name = XRT_INPUT_XBOX_DPAD_LEFT_CLICK;
	inputs_array[gp_shoulder_left].name = XRT_INPUT_XBOX_SHOULDER_LEFT_CLICK;
	inputs_array[gp_shoulder_right].name = XRT_INPUT_XBOX_SHOULDER_RIGHT_CLICK;
	inputs_array[gp_thumbstick_left_click].name = XRT_INPUT_XBOX_THUMBSTICK_LEFT_CLICK;
	inputs_array[gp_thumbstick_right_click].name = XRT_INPUT_XBOX_THUMBSTICK_RIGHT_CLICK;
	inputs_array[gp_thumbstick_left].name = XRT_INPUT_XBOX_THUMBSTICK_LEFT;
	inputs_array[gp_thumbstick_right].name = XRT_INPUT_XBOX_THUMBSTICK_RIGHT;
	inputs_array[gp_trigger_left].name = XRT_INPUT_XBOX_LEFT_TRIGGER_VALUE;
	inputs_array[gp_trigger_right].name = XRT_INPUT_XBOX_RIGHT_TRIGGER_VALUE;

	outputs_array = {{
	        {.name = XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT},
	        {.name = XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHT},
	        {.name = XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT_TRIGGER},
	        {.name = XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHT_TRIGGER},
	}};
	output_count = outputs_array.size();
	outputs = outputs_array.data();
}

xrt_result_t wivrn_gamepad::update_inputs()
{
	std::lock_guard lock(mutex);

	const int64_t timestamp = os_monotonic_get_ns();
	for (size_t i = 0; i < inputs_array.size(); ++i)
	{
		inputs_array[i].value = inputs_staging[i].value;
		inputs_array[i].active = connected;
		inputs_array[i].timestamp = timestamp;
	}

	return XRT_SUCCESS;
}

xrt_result_t wivrn_gamepad::get_tracked_pose(xrt_input_name name, int64_t, xrt_space_relation * out_relation)
{
	// A gamepad has no tracked pose.
	*out_relation = {};
	U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
	return XRT_ERROR_INPUT_UNSUPPORTED;
}

void wivrn_gamepad::set_inputs(const from_headset::inputs & inputs)
{
	std::lock_guard lock(mutex);

	for (const auto & value: inputs.values)
	{
		auto m = map_gamepad(value.id);
		if (m.slot == gp_count)
			continue;
		auto & v = inputs_staging[m.slot].value;
		switch (m.kind)
		{
			case gp_value::button:
			case gp_value::dpad:
				v.boolean = value.value != 0;
				break;
			case gp_value::trigger:
				v.vec1.x = value.value;
				break;
			case gp_value::stick_x:
				v.vec2.x = value.value;
				break;
			case gp_value::stick_y:
				v.vec2.y = value.value;
				break;
		}
	}
}

xrt_result_t wivrn_gamepad::set_output(xrt_output_name name, const xrt_output_value * value)
{
	device_id id;
	switch (name)
	{
		case XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT:
			id = device_id::GAMEPAD_HAPTIC_LEFT;
			break;
		case XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHT:
			id = device_id::GAMEPAD_HAPTIC_RIGHT;
			break;
		case XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT_TRIGGER:
			id = device_id::GAMEPAD_HAPTIC_LEFT_TRIGGER;
			break;
		case XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHT_TRIGGER:
			id = device_id::GAMEPAD_HAPTIC_RIGHT_TRIGGER;
			break;
		default:
			return XRT_ERROR_OUTPUT_UNSUPPORTED;
	}

	try
	{
		cnx->send_stream(to_headset::haptics{
		        .id = id,
		        .duration = std::chrono::nanoseconds(value->vibration.duration_ns),
		        .frequency = value->vibration.frequency,
		        .amplitude = value->vibration.amplitude});
	}
	catch (...)
	{
		return XRT_ERROR_OUTPUT_REQUEST_FAILURE;
	}
	return XRT_SUCCESS;
}

void wivrn_gamepad::set_connected(bool value)
{
	std::lock_guard lock(mutex);
	connected = value;
	if (not connected)
		for (auto & input: inputs_staging)
			input.value = {};
}

} // namespace wivrn
