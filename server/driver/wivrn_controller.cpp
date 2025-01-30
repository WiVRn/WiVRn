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
#include "configuration.h"
#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include <array>
#include <magic_enum.hpp>
#include <numbers>
#include <optional>
#include <stdio.h>

#include <fstream>

#include "xrt/xrt_defines.h"

namespace wivrn
{

enum wivrn_controller_input_index
{
	WIVRN_CONTROLLER_AIM_POSE,
	WIVRN_CONTROLLER_GRIP_POSE,
	WIVRN_CONTROLLER_HAND_TRACKER,
	WIVRN_CONTROLLER_PALM_POSE,

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

static xrt_binding_input_pair simple_input_binding[] = {
        {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
};

static xrt_binding_output_pair simple_output_binding[] = {
        {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

static xrt_binding_profile wivrn_binding_profiles[] = {
        {
                .name = XRT_DEVICE_SIMPLE_CONTROLLER,
                .inputs = simple_input_binding,
                .input_count = std::size(simple_input_binding),
                .outputs = simple_output_binding,
                .output_count = std::size(simple_output_binding),
        },
};

static void wivrn_controller_destroy(xrt_device * xdev);

static xrt_result_t wivrn_controller_get_tracked_pose(xrt_device * xdev,
                                                      xrt_input_name name,
                                                      int64_t at_timestamp_ns,
                                                      xrt_space_relation * out_relation)
{
	*out_relation = static_cast<wivrn_controller *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
	return XRT_SUCCESS;
}

static void wivrn_controller_get_hand_tracking(xrt_device * xdev,
                                               xrt_input_name name,
                                               int64_t desired_timestamp_ns,
                                               xrt_hand_joint_set * out_value,
                                               int64_t * out_timestamp_ns);

static void wivrn_controller_set_output(struct xrt_device * xdev, enum xrt_output_name name, const union xrt_output_value * value);

static xrt_result_t wivrn_controller_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_controller *>(xdev)->update_inputs();
	return XRT_SUCCESS;
}

namespace
{
struct xrt_space_relation_csv_header
{};
} // namespace

static std::ostream & operator<<(std::ostream & out, const xrt_space_relation_csv_header &)
{
	for (auto [value, name]: magic_enum::enum_entries<xrt_space_relation_flags>())
	{
		if (value and value != XRT_SPACE_RELATION_BITMASK_ALL)
		{
			name = name.substr(strlen("XRT_SPACE_RELATION_"));
			name = name.substr(0, name.size() - strlen("_BIT"));
			out << name << ",";
		}
	}
	out << "x,y,z,";
	out << "qw,qx,qy,qz";
	return out;
}

static std::ostream & operator<<(std::ostream & out, const xrt_space_relation & rel)
{
	const auto & pos = rel.pose.position;
	const auto & o = rel.pose.orientation;
	for (const auto & [value, name]: magic_enum::enum_entries<xrt_space_relation_flags>())
	{
		if (value and value != XRT_SPACE_RELATION_BITMASK_ALL)
			out << bool(rel.relation_flags & value) << ',';
	}
	out << pos.x << ',' << pos.y << ',' << pos.z << ',';
	out << o.w << ',' << o.x << ',' << o.y << ',' << o.z;
	return out;
}

static std::mutex tracking_dump_mutex;
static std::ofstream & tracking_dump()
{
	static auto res = [] {
		std::ofstream res;
		if (auto wivrn_dump = std::getenv("WIVRN_DUMP_TRACKING"))
		{
			res.open(wivrn_dump);
			res << "device_id,"
			       "now_ns,"
			       "timestamp_ns,"
			       "extrapolation_ns,"
			       "receive/get,"
			    << xrt_space_relation_csv_header{} << std::endl;
		}
		return res;
	}();
	return res;
}

wivrn_controller::wivrn_controller(int hand_id,
                                   xrt_device * hmd,
                                   wivrn::wivrn_session * cnx) :
        xrt_device{},
        grip(hand_id == 0 ? device_id::LEFT_GRIP : device_id::RIGHT_GRIP),
        aim(hand_id == 0 ? device_id::LEFT_AIM : device_id::RIGHT_AIM),
        palm(hand_id == 0 ? device_id::LEFT_PALM : device_id::RIGHT_PALM),
        joints(hand_id),
        cnx(cnx)
{
	xrt_device * base = this;

	base->destroy = wivrn_controller_destroy;
	base->get_tracked_pose = wivrn_controller_get_tracked_pose;
	base->get_hand_tracking = wivrn_controller_get_hand_tracking;
	base->set_output = wivrn_controller_set_output;
	base->update_inputs = wivrn_controller_update_inputs;

	base->name = XRT_DEVICE_TOUCH_CONTROLLER;
	base->orientation_tracking_supported = true;
	base->hand_tracking_supported = cnx->get_info().hand_tracking;
	base->position_tracking_supported = true;

	base->tracking_origin = hmd->tracking_origin;

	inputs_array.resize(WIVRN_CONTROLLER_INPUT_COUNT);
	inputs = inputs_array.data();
	input_count = inputs_array.size();

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

	inputs[WIVRN_CONTROLLER_HAND_TRACKER].name = hand_id == 0 ? XRT_INPUT_GENERIC_HAND_TRACKING_LEFT : XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
	inputs[WIVRN_CONTROLLER_HAND_TRACKER].active = hand_tracking_supported;

	inputs[WIVRN_CONTROLLER_PALM_POSE].name = XRT_INPUT_GENERIC_PALM_POSE;
	inputs[WIVRN_CONTROLLER_PALM_POSE].active = cnx->get_info().palm_pose;

	if (auto grip_surface = configuration::read_user_configuration().grip_surface)
	{
		std::array<float, 3> angles = grip_surface.value();
		float deg_2_rad = std::numbers::pi / 180.0;
		xrt_vec3 rotation_angles{angles[0] * deg_2_rad, angles[1] * deg_2_rad, angles[2] * deg_2_rad};
		xrt_pose offset = XRT_POSE_IDENTITY;
		math_quat_from_euler_angles(&rotation_angles, &offset.orientation);

		palm.set_derived(&grip, offset, true);
		cnx->set_enabled(palm.device, false);
		inputs[WIVRN_CONTROLLER_PALM_POSE].active = true;
	}

	output_count = 1;
	outputs = &haptic_output;
	haptic_output.name = XRT_OUTPUT_NAME_TOUCH_HAPTIC;

	inputs_staging = inputs_array;

	switch (hand_id)
	{
		case 0:
			device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;

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

	binding_profile_count = std::size(wivrn_binding_profiles);
	binding_profiles = wivrn_binding_profiles;
}

void wivrn_controller::update_inputs()
{
	std::lock_guard _{mutex};
	inputs_array = inputs_staging;
}

void wivrn_controller::set_inputs(const from_headset::inputs & inputs, const clock_offset & clock_offset)
{
	std::lock_guard lock{mutex};
	for (const auto & input: inputs.values)
	{
		set_inputs(input.id, input.value, input.last_change_time ? clock_offset.from_headset(input.last_change_time) : 0);
	}
}

void wivrn_controller::set_inputs(device_id input_id, float value, int64_t last_change_time)
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
		inputs_staging[binding->input_id].timestamp = last_change_time;
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

xrt_space_relation wivrn_controller::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns)
{
	std::chrono::nanoseconds extrapolation_time;
	xrt_space_relation res;
	device_id device;
	switch (name)
	{
		case XRT_INPUT_TOUCH_AIM_POSE:
			std::tie(extrapolation_time, res, device) = aim.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_TOUCH_GRIP_POSE:
			std::tie(extrapolation_time, res, device) = grip.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_GENERIC_PALM_POSE:
			std::tie(extrapolation_time, res, device) = palm.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		default:
			U_LOG_W("Unknown input name requested");
			return {};
	}
	cnx->add_predict_offset(extrapolation_time);
	if (auto & out = tracking_dump())
	{
		std::lock_guard lock{tracking_dump_mutex};
		auto device = [&] {
		switch (name)
		{
			case XRT_INPUT_TOUCH_AIM_POSE: return aim.device;
			case XRT_INPUT_TOUCH_GRIP_POSE: return grip.device;
			case XRT_INPUT_GENERIC_PALM_POSE: return palm.device;
			default:
				assert(false);
				__builtin_unreachable();
		} }();
		out << magic_enum::enum_name(device) << ','
		    << os_monotonic_get_ns() << ','
		    << at_timestamp_ns << ','
		    << extrapolation_time.count() << ','
		    << "g,"
		    << res << std::endl;
		;
	}
	return res;
}

std::pair<xrt_hand_joint_set, int64_t> wivrn_controller::get_hand_tracking(xrt_input_name name, int64_t desired_timestamp_ns)
{
	switch (name)
	{
		case XRT_INPUT_GENERIC_HAND_TRACKING_LEFT:
		case XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT: {
			auto [extrapolation_time, data] = joints.get_at(desired_timestamp_ns);
			cnx->add_predict_offset(extrapolation_time);
			cnx->set_enabled(joints.hand_id == 0 ? to_headset::tracking_control::id::left_hand : to_headset::tracking_control::id::right_hand, true);
			return {data, desired_timestamp_ns};
		}

		default:
			U_LOG_W("Unknown input name requested");
			return {};
	}
}

void wivrn_controller::set_derived_pose(const from_headset::derived_pose & derived)
{
	auto list = [this](device_id id) -> pose_list * {
		for (auto item: {&grip, &aim, &palm})
		{
			if (item->device == id)
				return item;
		}
		return nullptr;
	};
	auto source = list(derived.source);
	auto target = list(derived.target);
	if (source and target)
	{
		target->set_derived(source, xrt_cast(derived.relation));
		if (source != target)
			cnx->set_enabled(derived.target, false);
	}
}

void wivrn_controller::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	if (not aim.update_tracking(tracking, offset))
		cnx->set_enabled(aim.device, false);
	if (not grip.update_tracking(tracking, offset))
		cnx->set_enabled(grip.device, false);
	if (not palm.update_tracking(tracking, offset))
		cnx->set_enabled(palm.device, false);
	if (auto & out = tracking_dump(); out and offset)
	{
		std::lock_guard lock{tracking_dump_mutex};
		auto now = os_monotonic_get_ns();
		for (const auto & pose: tracking.device_poses)
		{
			if (pose.device == aim.device or pose.device == grip.device or pose.device == palm.device)
				out << magic_enum::enum_name(pose.device) << ','
				    << now << ','
				    << offset.from_headset(tracking.timestamp) << ','
				    << tracking.timestamp - tracking.production_timestamp << ','
				    << "r,"
				    << pose_list::convert_pose(pose) << std::endl;
		}
	}
}

void wivrn_controller::update_hand_tracking(const from_headset::hand_tracking & tracking, const clock_offset & offset)
{
	if (not joints.update_tracking(tracking, offset))
		cnx->set_enabled(joints.hand_id == 0 ? to_headset::tracking_control::id::left_hand : to_headset::tracking_control::id::right_hand, false);
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
	static_cast<wivrn_controller *>(xdev)->unregister();
}

static void wivrn_controller_get_hand_tracking(xrt_device * xdev,
                                               xrt_input_name name,
                                               int64_t desired_timestamp_ns,
                                               xrt_hand_joint_set * out_value,
                                               int64_t * out_timestamp_ns)
{
	std::tie(*out_value, *out_timestamp_ns) = static_cast<wivrn_controller *>(xdev)->get_hand_tracking(name, desired_timestamp_ns);
}

static void wivrn_controller_set_output(struct xrt_device * xdev, enum xrt_output_name name, const union xrt_output_value * value)
{
	static_cast<wivrn_controller *>(xdev)->set_output(name, value);
}
} // namespace wivrn
