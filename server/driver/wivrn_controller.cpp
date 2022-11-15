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

#include "wivrn_controller.h"

#include "os/os_threading.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include <stdio.h>

#include "xrt/xrt_defines.h"

/*
 *
 * Defines & structs.
 *
 */

enum wivrn_controller_input_index
{
	WIVRN_CONTROLLER_AIM_POSE,
	WIVRN_CONTROLLER_GRIP_POSE,
	WIVRN_CONTROLLER_MENU_CLICK,                         // /user/hand/left/input/menu/click
	WIVRN_CONTROLLER_A_CLICK,                            // /user/hand/right/input/a/click
	WIVRN_CONTROLLER_A_TOUCH,                            // /user/hand/right/input/a/touch
	WIVRN_CONTROLLER_B_CLICK,                            // /user/hand/right/input/b/click
	WIVRN_CONTROLLER_B_TOUCH,                            // /user/hand/right/input/b/touch
	WIVRN_CONTROLLER_X_CLICK = WIVRN_CONTROLLER_A_CLICK, // /user/hand/left/input/x/click
	WIVRN_CONTROLLER_X_TOUCH = WIVRN_CONTROLLER_A_TOUCH, // /user/hand/left/input/x/touch
	WIVRN_CONTROLLER_Y_CLICK = WIVRN_CONTROLLER_B_CLICK, // /user/hand/left/input/y/click
	WIVRN_CONTROLLER_Y_TOUCH = WIVRN_CONTROLLER_B_TOUCH, // /user/hand/left/input/y/touch
	WIVRN_CONTROLLER_SQUEEZE_CLICK,                      // /user/hand/XXXX/input/squeeze/click
	WIVRN_CONTROLLER_SQUEEZE_VALUE,                      // /user/hand/XXXX/input/squeeze/value
	WIVRN_CONTROLLER_TRIGGER_CLICK,                      // /user/hand/XXXX/input/trigger/click
	WIVRN_CONTROLLER_TRIGGER_VALUE,                      // /user/hand/XXXX/input/trigger/value
	WIVRN_CONTROLLER_TRIGGER_TOUCH,                      // /user/hand/XXXX/input/trigger/touch
	WIVRN_CONTROLLER_THUMBSTICK,                         // /user/hand/XXXX/input/thumbstick/{x,y}
	WIVRN_CONTROLLER_THUMBSTICK_CLICK,                   // /user/hand/XXXX/input/thumbstick/click
	WIVRN_CONTROLLER_THUMBSTICK_TOUCH,                   // /user/hand/XXXX/input/thumbstick/touch
	WIVRN_CONTROLLER_THUMBREST_TOUCH,                    // /user/hand/XXXX/input/thumbrest/touch

	WIVRN_CONTROLLER_INPUT_COUNT
};

enum class wivrn_input_type
{
	BOOL,
	FLOAT,
	VEC2_X,
	VEC2_Y
};

struct wivrn_to_wivrn_controller_input
{
	wivrn_controller_input_index input_id;
	device_id wivrn_id;
	wivrn_input_type input_type;
};

// clang-format off
static const wivrn_to_wivrn_controller_input left_hand_bindings[] = {
	{WIVRN_CONTROLLER_MENU_CLICK,       device_id::MENU_CLICK, wivrn_input_type::BOOL},           // /user/hand/left/input/menu/click
	{WIVRN_CONTROLLER_X_CLICK,          device_id::X_CLICK, wivrn_input_type::BOOL},              // /user/hand/left/input/x/click
	{WIVRN_CONTROLLER_X_TOUCH,          device_id::X_TOUCH, wivrn_input_type::BOOL},              // /user/hand/left/input/x/touch
	{WIVRN_CONTROLLER_Y_CLICK,          device_id::Y_CLICK, wivrn_input_type::BOOL},              // /user/hand/left/input/y/click
	{WIVRN_CONTROLLER_Y_TOUCH,          device_id::Y_TOUCH, wivrn_input_type::BOOL},              // /user/hand/left/input/y/touch
	{WIVRN_CONTROLLER_MENU_CLICK,       device_id::MENU_CLICK, wivrn_input_type::BOOL},           // /user/hand/left/input/menu/click
	{WIVRN_CONTROLLER_SQUEEZE_VALUE,    device_id::LEFT_SQUEEZE_VALUE, wivrn_input_type::FLOAT},  // /user/hand/left/input/squeeze/value
	{WIVRN_CONTROLLER_TRIGGER_VALUE,    device_id::LEFT_TRIGGER_VALUE, wivrn_input_type::FLOAT},  // /user/hand/left/input/trigger/value
	{WIVRN_CONTROLLER_TRIGGER_TOUCH,    device_id::LEFT_TRIGGER_TOUCH, wivrn_input_type::BOOL},   // /user/hand/left/input/trigger/touch
	{WIVRN_CONTROLLER_THUMBSTICK,       device_id::LEFT_THUMBSTICK_X, wivrn_input_type::VEC2_X},  // /user/hand/left/input/thumbstick/x
	{WIVRN_CONTROLLER_THUMBSTICK,       device_id::LEFT_THUMBSTICK_Y, wivrn_input_type::VEC2_Y},  // /user/hand/left/input/thumbstick/y
	{WIVRN_CONTROLLER_THUMBSTICK_CLICK, device_id::LEFT_THUMBSTICK_CLICK, wivrn_input_type::BOOL},// /user/hand/left/input/thumbstick/click
	{WIVRN_CONTROLLER_THUMBSTICK_TOUCH, device_id::LEFT_THUMBSTICK_TOUCH, wivrn_input_type::BOOL},// /user/hand/left/input/thumbstick/touch
	{WIVRN_CONTROLLER_THUMBREST_TOUCH,  device_id::LEFT_THUMBREST_TOUCH, wivrn_input_type::BOOL}, // /user/hand/left/input/thumbrest/touch
};

static const wivrn_to_wivrn_controller_input right_hand_bindings[] = {
	{WIVRN_CONTROLLER_A_CLICK,          device_id::A_CLICK, wivrn_input_type::BOOL},               // /user/hand/right/input/a/click
	{WIVRN_CONTROLLER_A_TOUCH,          device_id::A_TOUCH, wivrn_input_type::BOOL},               // /user/hand/right/input/a/touch
	{WIVRN_CONTROLLER_B_CLICK,          device_id::B_CLICK, wivrn_input_type::BOOL},               // /user/hand/right/input/b/click
	{WIVRN_CONTROLLER_B_TOUCH,          device_id::B_TOUCH, wivrn_input_type::BOOL},               // /user/hand/right/input/b/touch
	{WIVRN_CONTROLLER_SQUEEZE_VALUE,    device_id::RIGHT_SQUEEZE_VALUE, wivrn_input_type::FLOAT},  // /user/hand/right/input/squeeze/value
	{WIVRN_CONTROLLER_TRIGGER_VALUE,    device_id::RIGHT_TRIGGER_VALUE, wivrn_input_type::FLOAT},  // /user/hand/right/input/trigger/value
	{WIVRN_CONTROLLER_TRIGGER_TOUCH,    device_id::RIGHT_TRIGGER_TOUCH, wivrn_input_type::BOOL},   // /user/hand/right/input/trigger/touch
	{WIVRN_CONTROLLER_THUMBSTICK,       device_id::RIGHT_THUMBSTICK_X, wivrn_input_type::VEC2_X},  // /user/hand/right/input/thumbstick/x
	{WIVRN_CONTROLLER_THUMBSTICK,       device_id::RIGHT_THUMBSTICK_Y, wivrn_input_type::VEC2_Y},  // /user/hand/right/input/thumbstick/y
	{WIVRN_CONTROLLER_THUMBSTICK_CLICK, device_id::RIGHT_THUMBSTICK_CLICK, wivrn_input_type::BOOL},// /user/hand/right/input/thumbstick/click
	{WIVRN_CONTROLLER_THUMBSTICK_TOUCH, device_id::RIGHT_THUMBSTICK_TOUCH, wivrn_input_type::BOOL},// /user/hand/right/input/thumbstick/touch
	{WIVRN_CONTROLLER_THUMBREST_TOUCH,  device_id::RIGHT_THUMBREST_TOUCH, wivrn_input_type::BOOL}, // /user/hand/right/input/thumbrest/touch
};
// clang-format on

static const size_t left_hand_bindings_count = ARRAY_SIZE(left_hand_bindings);
static const size_t right_hand_bindings_count = ARRAY_SIZE(right_hand_bindings);

static void wivrn_controller_destroy(xrt_device * xdev);

static void wivrn_controller_get_tracked_pose(xrt_device * xdev,
                                              xrt_input_name name,
                                              uint64_t at_timestamp_ns,
                                              xrt_space_relation * out_relation);

static void wivrn_controller_set_output(struct xrt_device * xdev, enum xrt_output_name name, const union xrt_output_value * value);

static void wivrn_controller_update_inputs(xrt_device * xdev);

wivrn_controller::wivrn_controller(int hand_id,
                                   xrt_device * hmd,
                                   std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx) :
        xrt_device{}, grip(hand_id == 0 ? device_id::LEFT_GRIP : device_id::RIGHT_GRIP), aim(hand_id == 0 ? device_id::LEFT_AIM : device_id::RIGHT_AIM), cnx(cnx)
{
	xrt_device * base = this;

	base->destroy = wivrn_controller_destroy;
	base->get_tracked_pose = wivrn_controller_get_tracked_pose;
	base->get_hand_tracking = NULL; // TODO
	base->set_output = wivrn_controller_set_output;
	base->update_inputs = wivrn_controller_update_inputs;

	base->name = XRT_DEVICE_TOUCH_CONTROLLER;
	base->orientation_tracking_supported = true;
	base->hand_tracking_supported = false;
	base->position_tracking_supported = true;

	base->tracking_origin = hmd->tracking_origin;

	inputs_array.resize(WIVRN_CONTROLLER_INPUT_COUNT);
	inputs = inputs_array.data();
	input_count = WIVRN_CONTROLLER_INPUT_COUNT;

	// Setup input.
#define SET_INPUT(NAME)                                                        \
	do                                                                     \
	{                                                                      \
		inputs[WIVRN_CONTROLLER_##NAME].name = XRT_INPUT_TOUCH_##NAME; \
		inputs[WIVRN_CONTROLLER_##NAME].active = true;                 \
	} while (0)

	SET_INPUT(AIM_POSE);
	SET_INPUT(GRIP_POSE);
	if (hand_id == 0)
	{
		SET_INPUT(X_CLICK);
		SET_INPUT(Y_CLICK);
		SET_INPUT(X_TOUCH);
		SET_INPUT(Y_TOUCH);
		SET_INPUT(MENU_CLICK);
	}
	else
	{
		SET_INPUT(A_CLICK);
		SET_INPUT(B_CLICK);
		SET_INPUT(A_TOUCH);
		SET_INPUT(B_TOUCH);
	}
	SET_INPUT(SQUEEZE_VALUE);
	SET_INPUT(TRIGGER_VALUE);
	SET_INPUT(TRIGGER_TOUCH);
	SET_INPUT(THUMBSTICK);
	SET_INPUT(THUMBSTICK_CLICK);
	SET_INPUT(THUMBSTICK_TOUCH);
	SET_INPUT(THUMBREST_TOUCH);

	output_count = 1;
	outputs = &haptic_output;
	haptic_output.name = XRT_OUTPUT_NAME_TOUCH_HAPTIC;

	inputs_staging = inputs_array;

	switch (hand_id)
	{
		case 0:
			device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
			SET_INPUT(MENU_CLICK);

			// Print name.
			strcpy(str, "WiVRn HMD left hand controller");
			strcpy(serial, "WiVRn HMD left hand controller");

			break;

		case 1:
			device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;

			// Print name.
			strcpy(str, "WiVRn HMD right hand controller");
			strcpy(serial, "WiVRn HMD right hand controller");

			break;

		default:
			throw std::runtime_error("Invalid hand ID");
	}
}

void wivrn_controller::update_inputs()
{
	std::lock_guard _{mutex};
	inputs_array = inputs_staging;
}

void wivrn_controller::set_inputs(const from_headset::inputs & inputs)
{
	std::lock_guard lock{mutex};
	for (const auto & input: inputs.values)
	{
		set_inputs(input.id, input.value);
	}
}

void wivrn_controller::set_inputs(device_id input_id, float value)
{
	const struct wivrn_to_wivrn_controller_input * bindings;
	size_t bindings_count;
	if (device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER)
	{
		bindings = left_hand_bindings;
		bindings_count = left_hand_bindings_count;
	}
	else
	{
		bindings = right_hand_bindings;
		bindings_count = right_hand_bindings_count;
	}

	const struct wivrn_to_wivrn_controller_input * binding = NULL;
	for (size_t i = 0; i < bindings_count; i++)
	{
		if (bindings[i].wivrn_id == input_id)
		{
			binding = &bindings[i];
			break;
		}
	}

	if (binding)
	{
		switch (binding->input_type)
		{
			case wivrn_input_type::BOOL:
				inputs_staging[binding->input_id].value.boolean = (value != 0);
				break;
			case wivrn_input_type::FLOAT:
				inputs_staging[binding->input_id].value.vec1.x = value;
				break;
			case wivrn_input_type::VEC2_X:
				inputs_staging[binding->input_id].value.vec2.x = value;
				break;
			case wivrn_input_type::VEC2_Y:
				inputs_staging[binding->input_id].value.vec2.y = value;
				break;
		}
	}
}

xrt_space_relation wivrn_controller::get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns)
{
	switch (name)
	{
		case XRT_INPUT_TOUCH_AIM_POSE:
			return aim.get_at(at_timestamp_ns);

		case XRT_INPUT_TOUCH_GRIP_POSE:
			return grip.get_at(at_timestamp_ns);

		default:
			U_LOG_W("Unknown input name requested");
			return {};
	}
}

void wivrn_controller::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	aim.update_tracking(tracking, offset);
	grip.update_tracking(tracking, offset);
}

void wivrn_controller::set_output(xrt_output_name name, const xrt_output_value * value)
{
	auto id = device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER ? device_id::LEFT_CONTROLLER_HAPTIC
	                                                              : device_id::RIGHT_CONTROLLER_HAPTIC;

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
		// Ignore errors
	}
}

/*
 *
 * Functions
 *
 */

static void wivrn_controller_destroy(xrt_device * xdev)
{
	delete (wivrn_controller *)xdev;
}

static void wivrn_controller_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_controller *>(xdev)->update_inputs();
}

static void wivrn_controller_get_tracked_pose(xrt_device * xdev,
                                              xrt_input_name name,
                                              uint64_t at_timestamp_ns,
                                              xrt_space_relation * out_relation)
{
	*out_relation = static_cast<wivrn_controller *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
}

static void wivrn_controller_set_output(struct xrt_device * xdev, enum xrt_output_name name, const union xrt_output_value * value)
{
	static_cast<wivrn_controller *>(xdev)->set_output(name, value);
}
