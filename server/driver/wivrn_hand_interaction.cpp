/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_hand_interaction.h"
#include "configuration.h"
#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "utils/method.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include <array>
#include <magic_enum.hpp>
#include <numbers>
#include <optional>

#include <fstream>

#include "xrt/xrt_defines.h"

namespace wivrn
{

enum wivrn_hand_interaction_input_index
{
	WIVRN_HAND_INTERACTION_INPUT_INVALID = -1,
	WIVRN_HAND_INTERACTION_AIM_POSE,
	WIVRN_HAND_INTERACTION_GRIP_POSE,
	WIVRN_HAND_INTERACTION_PALM_POSE,

	WIVRN_HAND_INTERACTION_PINCH_POSE,         // /user/hand/XXXX/input/pinch_ext/pose
	WIVRN_HAND_INTERACTION_PINCH_VALUE,        // /user/hand/XXXX/input/pinch_ext/value
	WIVRN_HAND_INTERACTION_PINCH_READY,        // /user/hand/XXXX/input/pinch_ext/ready_ext
	WIVRN_HAND_INTERACTION_POKE_POSE,          // /user/hand/XXXX/input/poke_ext/pose
	WIVRN_HAND_INTERACTION_AIM_ACTIVATE_VALUE, // /user/hand/XXXX/input/aim_activate_ext/value
	WIVRN_HAND_INTERACTION_AIM_ACTIVATE_READY, // /user/hand/XXXX/input/aim_activate_ext/ready_ext
	WIVRN_HAND_INTERACTION_GRASP_VALUE,        // /user/hand/XXXX/input/grasp_ext/value
	WIVRN_HAND_INTERACTION_GRASP_READY,        // /user/hand/XXXX/input/ready_ext/value

	WIVRN_HAND_INTERACTION_INPUT_COUNT
};

enum class wivrn_input_type
{
	BOOL,
	FLOAT,
	VEC2_X,
	VEC2_Y,
	POSE,
};

struct input_data
{
	wivrn_hand_interaction_input_index index;
	wivrn_input_type type;
	xrt_device_type device;
};

static input_data map_input(device_id id)
{
	switch (id)
	{
		case device_id::LEFT_GRIP:
			return {WIVRN_HAND_INTERACTION_GRIP_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_GRIP:
			return {WIVRN_HAND_INTERACTION_GRIP_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_AIM:
			return {WIVRN_HAND_INTERACTION_AIM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM:
			return {WIVRN_HAND_INTERACTION_AIM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_PALM:
			return {WIVRN_HAND_INTERACTION_PALM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_PALM:
			return {WIVRN_HAND_INTERACTION_PALM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		// XR_EXT_hand_interaction
		case device_id::LEFT_PINCH_POSE:
			return {WIVRN_HAND_INTERACTION_PINCH_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_PINCH_VALUE:
			return {WIVRN_HAND_INTERACTION_PINCH_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_PINCH_READY:
			return {WIVRN_HAND_INTERACTION_PINCH_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_POSE:
			return {WIVRN_HAND_INTERACTION_PINCH_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_VALUE:
			return {WIVRN_HAND_INTERACTION_PINCH_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_READY:
			return {WIVRN_HAND_INTERACTION_PINCH_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_POKE:
			return {WIVRN_HAND_INTERACTION_POKE_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_POKE:
			return {WIVRN_HAND_INTERACTION_POKE_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_AIM_ACTIVATE_VALUE:
			return {WIVRN_HAND_INTERACTION_AIM_ACTIVATE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_AIM_ACTIVATE_READY:
			return {WIVRN_HAND_INTERACTION_AIM_ACTIVATE_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_GRASP_VALUE:
			return {WIVRN_HAND_INTERACTION_GRASP_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_GRASP_READY:
			return {WIVRN_HAND_INTERACTION_GRASP_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM_ACTIVATE_VALUE:
			return {WIVRN_HAND_INTERACTION_AIM_ACTIVATE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM_ACTIVATE_READY:
			return {WIVRN_HAND_INTERACTION_AIM_ACTIVATE_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_GRASP_VALUE:
			return {WIVRN_HAND_INTERACTION_GRASP_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_GRASP_READY:
			return {WIVRN_HAND_INTERACTION_GRASP_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		default:
			break;
	}
	// If the headset supports hand_interaction_ext, upon switch
	// to/from hand tracking we may get the inputs packet before
	// the interaction profile change, so if we get a bad input
	// just return an invalid index so we can ignore it
	U_LOG_D("wivrn_hand_interaction: bad input id %s", std::string(magic_enum::enum_name(id)).c_str());
	// the type here doesn't really matter
	return {WIVRN_HAND_INTERACTION_INPUT_INVALID, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_UNKNOWN};
}

static xrt_binding_input_pair hand_interaction_input_binding[] = {
        {XRT_INPUT_HAND_GRIP_POSE, XRT_INPUT_HAND_GRIP_POSE},
        {XRT_INPUT_HAND_AIM_POSE, XRT_INPUT_HAND_AIM_POSE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_PINCH_VALUE, XRT_INPUT_HAND_PINCH_VALUE},
        {XRT_INPUT_HAND_PINCH_READY, XRT_INPUT_HAND_PINCH_READY},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
        {XRT_INPUT_HAND_AIM_ACTIVATE_READY, XRT_INPUT_HAND_AIM_ACTIVATE_READY},
        {XRT_INPUT_HAND_AIM_ACTIVATE_VALUE, XRT_INPUT_HAND_AIM_ACTIVATE_VALUE},
        {XRT_INPUT_HAND_GRASP_VALUE, XRT_INPUT_HAND_GRASP_VALUE},
        {XRT_INPUT_HAND_GRASP_READY, XRT_INPUT_HAND_GRASP_READY},
};

static xrt_binding_profile wivrn_binding_profiles[] = {
        {
                .name = XRT_DEVICE_EXT_HAND_INTERACTION,
                .inputs = hand_interaction_input_binding,
                .input_count = std::size(hand_interaction_input_binding),
                .outputs = nullptr,
                .output_count = 0,
        },
};

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

wivrn_hand_interaction::wivrn_hand_interaction(int hand_id,
                                               xrt_device * hmd,
                                               wivrn::wivrn_session * cnx) :
        xrt_device{
                .name = XRT_DEVICE_EXT_HAND_INTERACTION,
                .device_type = hand_id == 0 ? XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER : XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER,
                .hmd = nullptr,
                .tracking_origin = hmd->tracking_origin,
                .binding_profile_count = std::size(wivrn_binding_profiles),
                .binding_profiles = wivrn_binding_profiles,
                .input_count = WIVRN_HAND_INTERACTION_INPUT_COUNT,
                .supported = {
                        .orientation_tracking = true,
                        .position_tracking = true,
                },
                .update_inputs = method_pointer<&wivrn_hand_interaction::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_hand_interaction::get_tracked_pose>,
                .destroy = [](xrt_device *) {},
        },
        grip(hand_id == 0 ? device_id::LEFT_GRIP : device_id::RIGHT_GRIP),
        aim(hand_id == 0 ? device_id::LEFT_AIM : device_id::RIGHT_AIM),
        palm(hand_id == 0 ? device_id::LEFT_PALM : device_id::RIGHT_PALM),
        pinch_ext(hand_id == 0 ? device_id::LEFT_PINCH_POSE : device_id::RIGHT_PINCH_POSE),
        poke_ext(hand_id == 0 ? device_id::LEFT_POKE : device_id::RIGHT_POKE),
        inputs_array(input_count, xrt_input{}),
        cnx(cnx)
{
	inputs = inputs_array.data();

	// Setup input.
#define SET_INPUT(VENDOR, NAME)                                                           \
	do                                                                                \
	{                                                                                 \
		inputs[WIVRN_HAND_INTERACTION_##NAME].name = XRT_INPUT_##VENDOR##_##NAME; \
		inputs[WIVRN_HAND_INTERACTION_##NAME].active = true;                      \
	} while (0)

	if (auto grip_surface = configuration().grip_surface)
	{
		std::array<float, 3> angles = grip_surface.value();
		float deg_2_rad = std::numbers::pi / 180.0;
		xrt_vec3 rotation_angles{angles[0] * deg_2_rad, angles[1] * deg_2_rad, angles[2] * deg_2_rad};
		xrt_pose offset = XRT_POSE_IDENTITY;
		math_quat_from_euler_angles(&rotation_angles, &offset.orientation);

		palm.set_derived(&grip, offset, true);
		cnx->set_enabled(palm.device, false);
	}
	else if (not cnx->get_info().palm_pose)
	{
		palm.set_derived(&grip, XRT_POSE_IDENTITY, false);
		cnx->set_enabled(palm.device, false);
	}

	SET_INPUT(HAND, AIM_POSE);
	SET_INPUT(HAND, GRIP_POSE);
	SET_INPUT(GENERIC, PALM_POSE);
	SET_INPUT(HAND, PINCH_POSE);
	SET_INPUT(HAND, PINCH_VALUE);
	SET_INPUT(HAND, PINCH_READY);
	SET_INPUT(HAND, POKE_POSE);
	SET_INPUT(HAND, AIM_ACTIVATE_VALUE);
	SET_INPUT(HAND, AIM_ACTIVATE_READY);
	SET_INPUT(HAND, GRASP_VALUE);
	SET_INPUT(HAND, GRASP_READY);

#undef SET_INPUT
	inputs_staging = inputs_array;

	// Make sure everything is mapped
	for (const auto & item: inputs_array)
	{
		assert(item.name);
	}

	outputs = nullptr;
	output_count = 0;

	switch (hand_id)
	{
		case 0:
			// Print name.
			strcpy(str, "WiVRn left hand interaction");
			strcpy(serial, "WiVRn left hand interaction");
			break;
		case 1:
			// Print name.
			strcpy(str, "WiVRn right hand interaction");
			strcpy(serial, "WiVRn right hand interaction");
			break;
		default:
			throw std::runtime_error("Invalid hand ID");
	}
}

xrt_result_t wivrn_hand_interaction::update_inputs()
{
	std::lock_guard _{mutex};
	inputs_array = inputs_staging;
	return XRT_SUCCESS;
}

void wivrn_hand_interaction::set_inputs(const from_headset::inputs & inputs, const clock_offset & clock_offset)
{
	std::lock_guard lock{mutex};
	for (auto & input: inputs_staging)
	{
		switch (XRT_GET_INPUT_TYPE(input.name))
		{
			case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE:
			case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE:
			case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE:
			case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE:
			case XRT_INPUT_TYPE_BOOLEAN:
				input.active = false;
			case XRT_INPUT_TYPE_POSE:
			case XRT_INPUT_TYPE_HAND_TRACKING:
			case XRT_INPUT_TYPE_FACE_TRACKING:
			case XRT_INPUT_TYPE_BODY_TRACKING:
				break;
		}
	}

	for (const auto & input: inputs.values)
	{
		int64_t last_change_time = input.last_change_time ? clock_offset.from_headset(input.last_change_time) : 0;
		auto [index, type, device] = map_input(input.id);
		if (device != device_type)
			continue;
		assert(index > 0 and index < input_count);
		inputs_staging[index].timestamp = last_change_time;
		inputs_staging[index].active = true;
		switch (type)
		{
			case wivrn_input_type::BOOL:
				inputs_staging[index].value.boolean = (input.value != 0);
				break;
			case wivrn_input_type::FLOAT:
				inputs_staging[index].value.vec1.x = input.value;
				break;
			case wivrn_input_type::VEC2_X:
				inputs_staging[index].value.vec2.x = input.value;
				break;
			case wivrn_input_type::VEC2_Y:
				inputs_staging[index].value.vec2.y = input.value;
				break;
			case wivrn_input_type::POSE:
				// Pose should not be in the inputs array
				U_LOG_W("Unexpected input id %d", int(input.id));
		}
	}
}

xrt_result_t wivrn_hand_interaction::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * res)
{
	std::chrono::nanoseconds extrapolation_time;
	device_id device;
	switch (name)
	{
		case XRT_INPUT_HAND_AIM_POSE:
			std::tie(extrapolation_time, *res, device) = aim.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_HAND_GRIP_POSE:
			std::tie(extrapolation_time, *res, device) = grip.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_GENERIC_PALM_POSE:
			std::tie(extrapolation_time, *res, device) = palm.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_HAND_PINCH_POSE:
			std::tie(extrapolation_time, *res, device) = pinch_ext.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		case XRT_INPUT_HAND_POKE_POSE:
			std::tie(extrapolation_time, *res, device) = poke_ext.get_pose_at(at_timestamp_ns);
			cnx->set_enabled(device, true);
			break;
		default:
			U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
			return XRT_ERROR_INPUT_UNSUPPORTED;
	}
	cnx->add_predict_offset(extrapolation_time);
	if (auto & out = tracking_dump())
	{
		std::lock_guard lock{tracking_dump_mutex};
		auto device = [&] {
		switch (name)
		{
			case XRT_INPUT_HAND_AIM_POSE: return aim.device;
			case XRT_INPUT_HAND_GRIP_POSE: return grip.device;
			case XRT_INPUT_GENERIC_PALM_POSE: return palm.device;
			case XRT_INPUT_HAND_PINCH_POSE: return pinch_ext.device;
			case XRT_INPUT_HAND_POKE_POSE: return poke_ext.device;
			default:
				assert(false);
				__builtin_unreachable();
		} }();
		out << magic_enum::enum_name(device) << ','
		    << os_monotonic_get_ns() << ','
		    << at_timestamp_ns << ','
		    << extrapolation_time.count() << ','
		    << "g,"
		    << *res << std::endl;
		;
	}
	return XRT_SUCCESS;
}

void wivrn_hand_interaction::set_derived_pose(const from_headset::derived_pose & derived)
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

void wivrn_hand_interaction::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	if (not aim.update_tracking(tracking, offset))
		cnx->set_enabled(aim.device, false);
	if (not grip.update_tracking(tracking, offset))
		cnx->set_enabled(grip.device, false);
	if (not palm.update_tracking(tracking, offset))
		cnx->set_enabled(palm.device, false);
	if (not pinch_ext.update_tracking(tracking, offset))
		cnx->set_enabled(pinch_ext.device, false);
	if (not poke_ext.update_tracking(tracking, offset))
		cnx->set_enabled(poke_ext.device, false);
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

void wivrn_hand_interaction::reset_history()
{
	aim.reset();
	grip.reset();
	palm.reset();
	pinch_ext.reset();
	poke_ext.reset();
}
} // namespace wivrn
