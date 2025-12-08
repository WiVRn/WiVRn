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

#include "wivrn_controller.h"
#include "configuration.h"
#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "utils/method.h"
#include "wivrn_session.h"

#include "util/u_logging.h"
#include <array>
#include <format>
#include <fstream>
#include <magic_enum.hpp>
#include <numbers>
#include <optional>
#include <stdexcept>

#include "xrt/xrt_defines.h"

namespace wivrn
{

namespace
{
enum wivrn_controller_input_index
{
	WIVRN_CONTROLLER_INPUT_INVALID = -1,
	WIVRN_CONTROLLER_AIM_POSE,
	WIVRN_CONTROLLER_GRIP_POSE,
	WIVRN_CONTROLLER_PALM_POSE,
	WIVRN_CONTROLLER_HAND_TRACKING_LEFT,
	WIVRN_CONTROLLER_HAND_TRACKING_RIGHT = WIVRN_CONTROLLER_HAND_TRACKING_LEFT,

	WIVRN_CONTROLLER_MENU_CLICK,                         // /user/hand/left/input/menu/click
	WIVRN_CONTROLLER_SYSTEM_CLICK                        // /user/hand/right/input/system/click
	= WIVRN_CONTROLLER_MENU_CLICK,                       //
	WIVRN_CONTROLLER_A_CLICK,                            // /user/hand/right/input/a/click
	WIVRN_CONTROLLER_A_TOUCH,                            // /user/hand/right/input/a/touch
	WIVRN_CONTROLLER_B_CLICK,                            // /user/hand/right/input/b/click
	WIVRN_CONTROLLER_B_TOUCH,                            // /user/hand/right/input/b/touch
	WIVRN_CONTROLLER_X_CLICK = WIVRN_CONTROLLER_A_CLICK, // /user/hand/left/input/x/click
	WIVRN_CONTROLLER_X_TOUCH = WIVRN_CONTROLLER_A_TOUCH, // /user/hand/left/input/x/touch
	WIVRN_CONTROLLER_Y_CLICK = WIVRN_CONTROLLER_B_CLICK, // /user/hand/left/input/y/click
	WIVRN_CONTROLLER_Y_TOUCH = WIVRN_CONTROLLER_B_TOUCH, // /user/hand/left/input/y/touch
	WIVRN_CONTROLLER_SQUEEZE_CLICK,                      // /user/hand/XXXX/input/squeeze/click
	WIVRN_CONTROLLER_SQUEEZE_FORCE,                      // /user/hand/XXXX/input/squeeze/force
	WIVRN_CONTROLLER_SQUEEZE_VALUE,                      // /user/hand/XXXX/input/squeeze/value
	WIVRN_CONTROLLER_TRIGGER_CLICK,                      // /user/hand/XXXX/input/trigger/click
	WIVRN_CONTROLLER_TRIGGER_VALUE,                      // /user/hand/XXXX/input/trigger/value
	WIVRN_CONTROLLER_TRIGGER_TOUCH,                      // /user/hand/XXXX/input/trigger/touch
	WIVRN_CONTROLLER_TRIGGER_PROXIMITY,                  // /user/hand/XXXX/input/trigger/proximity
	WIVRN_CONTROLLER_TRIGGER_CURL,                       // /user/hand/XXXX/input/trigger/curl_fb
	WIVRN_CONTROLLER_TRIGGER_SLIDE,                      // /user/hand/XXXX/input/trigger/slide_fb
	WIVRN_CONTROLLER_TRIGGER_FORCE,                      // /user/hand/XXXX/input/trigger/force
	WIVRN_CONTROLLER_THUMBSTICK,                         // /user/hand/XXXX/input/thumbstick/{x,y}
	WIVRN_CONTROLLER_THUMBSTICK_CLICK,                   // /user/hand/XXXX/input/thumbstick/click
	WIVRN_CONTROLLER_THUMBSTICK_TOUCH,                   // /user/hand/XXXX/input/thumbstick/touch
	WIVRN_CONTROLLER_THUMBREST_TOUCH,                    // /user/hand/XXXX/input/thumbrest/touch
	WIVRN_CONTROLLER_THUMBREST_FORCE,                    // /user/hand/XXXX/input/thumbrest/force
	WIVRN_CONTROLLER_THUMB_PROXIMITY,                    // /user/hand/XXXX/input/thumb_resting_surfaces/proximity
	WIVRN_CONTROLLER_TRACKPAD,                           // /user/hand/XXXX/input/trackpad/{x,y}
	WIVRN_CONTROLLER_TRACKPAD_CLICK,                     // /user/hand/XXXX/input/trackpad/click
	WIVRN_CONTROLLER_TRACKPAD_FORCE,                     // /user/hand/XXXX/input/trackpad/force
	WIVRN_CONTROLLER_TRACKPAD_TOUCH,                     // /user/hand/XXXX/input/trackpad/touch
	WIVRN_CONTROLLER_STYLUS_FORCE,                       // /user/hand/XXXX/input/stylus_fb/force

	// XR_EXT_hand_interaction
	WIVRN_CONTROLLER_PINCH_POSE,         // /user/hand/XXXX/input/pinch_ext/pose
	WIVRN_CONTROLLER_PINCH_VALUE,        // /user/hand/XXXX/input/pinch_ext/value
	WIVRN_CONTROLLER_PINCH_READY,        // /user/hand/XXXX/input/pinch_ext/ready_ext
	WIVRN_CONTROLLER_POKE_POSE,          // /user/hand/XXXX/input/poke_ext/pose
	WIVRN_CONTROLLER_AIM_ACTIVATE_VALUE, // /user/hand/XXXX/input/aim_activate_ext/value
	WIVRN_CONTROLLER_AIM_ACTIVATE_READY, // /user/hand/XXXX/input/aim_activate_ext/ready_ext
	WIVRN_CONTROLLER_GRASP_VALUE,        // /user/hand/XXXX/input/grasp_ext/value
	WIVRN_CONTROLLER_GRASP_READY,        // /user/hand/XXXX/input/ready_ext/value

	WIVRN_CONTROLLER_INPUT_COUNT
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
	wivrn_controller_input_index index;
	wivrn_input_type type;
	xrt_device_type device;
};

input_data map_input(device_id id)
{
	switch (id)
	{
		case device_id::HEAD:
		case device_id::EYE_GAZE:
		case device_id::LEFT_CONTROLLER_HAPTIC:
		case device_id::RIGHT_CONTROLLER_HAPTIC:
		case device_id::LEFT_TRIGGER_HAPTIC:
		case device_id::RIGHT_TRIGGER_HAPTIC:
		case device_id::LEFT_THUMB_HAPTIC:
		case device_id::RIGHT_THUMB_HAPTIC:
			break;
		case device_id::LEFT_GRIP:
			return {WIVRN_CONTROLLER_GRIP_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_GRIP:
			return {WIVRN_CONTROLLER_GRIP_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_AIM:
			return {WIVRN_CONTROLLER_AIM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM:
			return {WIVRN_CONTROLLER_AIM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_PALM:
			return {WIVRN_CONTROLLER_PALM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_PALM:
			return {WIVRN_CONTROLLER_PALM_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::X_CLICK:
			return {WIVRN_CONTROLLER_X_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::A_CLICK:
			return {WIVRN_CONTROLLER_A_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::X_TOUCH:
			return {WIVRN_CONTROLLER_X_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::A_TOUCH:
			return {WIVRN_CONTROLLER_A_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::Y_CLICK:
			return {WIVRN_CONTROLLER_Y_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::B_CLICK:
			return {WIVRN_CONTROLLER_B_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::Y_TOUCH:
			return {WIVRN_CONTROLLER_Y_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::B_TOUCH:
			return {WIVRN_CONTROLLER_B_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::MENU_CLICK:
			return {WIVRN_CONTROLLER_MENU_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::SYSTEM_CLICK:
			return {WIVRN_CONTROLLER_SYSTEM_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_SQUEEZE_VALUE:
			return {WIVRN_CONTROLLER_SQUEEZE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_SQUEEZE_VALUE:
			return {WIVRN_CONTROLLER_SQUEEZE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_VALUE:
			return {WIVRN_CONTROLLER_TRIGGER_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_VALUE:
			return {WIVRN_CONTROLLER_TRIGGER_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_TOUCH:
			return {WIVRN_CONTROLLER_TRIGGER_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_TOUCH:
			return {WIVRN_CONTROLLER_TRIGGER_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBSTICK_X:
			return {WIVRN_CONTROLLER_THUMBSTICK, wivrn_input_type::VEC2_X, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBSTICK_X:
			return {WIVRN_CONTROLLER_THUMBSTICK, wivrn_input_type::VEC2_X, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBSTICK_Y:
			return {WIVRN_CONTROLLER_THUMBSTICK, wivrn_input_type::VEC2_Y, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBSTICK_Y:
			return {WIVRN_CONTROLLER_THUMBSTICK, wivrn_input_type::VEC2_Y, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBSTICK_CLICK:
			return {WIVRN_CONTROLLER_THUMBSTICK_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBSTICK_CLICK:
			return {WIVRN_CONTROLLER_THUMBSTICK_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBSTICK_TOUCH:
			return {WIVRN_CONTROLLER_THUMBSTICK_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBSTICK_TOUCH:
			return {WIVRN_CONTROLLER_THUMBSTICK_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBREST_TOUCH:
			return {WIVRN_CONTROLLER_THUMBREST_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBREST_TOUCH:
			return {WIVRN_CONTROLLER_THUMBREST_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_SQUEEZE_CLICK:
			return {WIVRN_CONTROLLER_SQUEEZE_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_SQUEEZE_CLICK:
			return {WIVRN_CONTROLLER_SQUEEZE_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_SQUEEZE_FORCE:
			return {WIVRN_CONTROLLER_SQUEEZE_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_SQUEEZE_FORCE:
			return {WIVRN_CONTROLLER_SQUEEZE_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_CLICK:
			return {WIVRN_CONTROLLER_TRIGGER_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_CLICK:
			return {WIVRN_CONTROLLER_TRIGGER_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_PROXIMITY:
			return {WIVRN_CONTROLLER_TRIGGER_PROXIMITY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_PROXIMITY:
			return {WIVRN_CONTROLLER_TRIGGER_PROXIMITY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_THUMB_PROXIMITY:
			return {WIVRN_CONTROLLER_THUMB_PROXIMITY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMB_PROXIMITY:
			return {WIVRN_CONTROLLER_THUMB_PROXIMITY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRACKPAD_X:
			return {WIVRN_CONTROLLER_TRACKPAD, wivrn_input_type::VEC2_X, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRACKPAD_X:
			return {WIVRN_CONTROLLER_TRACKPAD, wivrn_input_type::VEC2_X, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRACKPAD_Y:
			return {WIVRN_CONTROLLER_TRACKPAD, wivrn_input_type::VEC2_Y, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRACKPAD_Y:
			return {WIVRN_CONTROLLER_TRACKPAD, wivrn_input_type::VEC2_Y, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRACKPAD_CLICK:
			return {WIVRN_CONTROLLER_TRACKPAD_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRACKPAD_CLICK:
			return {WIVRN_CONTROLLER_TRACKPAD_CLICK, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRACKPAD_TOUCH:
			return {WIVRN_CONTROLLER_TRACKPAD_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRACKPAD_TOUCH:
			return {WIVRN_CONTROLLER_TRACKPAD_TOUCH, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRACKPAD_FORCE:
			return {WIVRN_CONTROLLER_TRACKPAD_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRACKPAD_FORCE:
			return {WIVRN_CONTROLLER_TRACKPAD_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_CURL:
			return {WIVRN_CONTROLLER_TRIGGER_CURL, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_SLIDE:
			return {WIVRN_CONTROLLER_TRIGGER_SLIDE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_TRIGGER_FORCE:
			return {WIVRN_CONTROLLER_TRIGGER_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_THUMBREST_FORCE:
			return {WIVRN_CONTROLLER_THUMBREST_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_STYLUS_FORCE:
			return {WIVRN_CONTROLLER_STYLUS_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_CURL:
			return {WIVRN_CONTROLLER_TRIGGER_CURL, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_SLIDE:
			return {WIVRN_CONTROLLER_TRIGGER_SLIDE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_TRIGGER_FORCE:
			return {WIVRN_CONTROLLER_TRIGGER_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_THUMBREST_FORCE:
			return {WIVRN_CONTROLLER_THUMBREST_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_STYLUS_FORCE:
			return {WIVRN_CONTROLLER_STYLUS_FORCE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		// XR_EXT_hand_interaction
		case device_id::LEFT_PINCH_POSE:
			return {WIVRN_CONTROLLER_PINCH_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_PINCH_VALUE:
			return {WIVRN_CONTROLLER_PINCH_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_PINCH_READY:
			return {WIVRN_CONTROLLER_PINCH_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_POSE:
			return {WIVRN_CONTROLLER_PINCH_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_VALUE:
			return {WIVRN_CONTROLLER_PINCH_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_PINCH_READY:
			return {WIVRN_CONTROLLER_PINCH_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_POKE:
			return {WIVRN_CONTROLLER_POKE_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_POKE:
			return {WIVRN_CONTROLLER_POKE_POSE, wivrn_input_type::POSE, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_AIM_ACTIVATE_VALUE:
			return {WIVRN_CONTROLLER_AIM_ACTIVATE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_AIM_ACTIVATE_READY:
			return {WIVRN_CONTROLLER_AIM_ACTIVATE_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM_ACTIVATE_VALUE:
			return {WIVRN_CONTROLLER_AIM_ACTIVATE_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_AIM_ACTIVATE_READY:
			return {WIVRN_CONTROLLER_AIM_ACTIVATE_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::LEFT_GRASP_VALUE:
			return {WIVRN_CONTROLLER_GRASP_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::LEFT_GRASP_READY:
			return {WIVRN_CONTROLLER_GRASP_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER};
		case device_id::RIGHT_GRASP_VALUE:
			return {WIVRN_CONTROLLER_GRASP_VALUE, wivrn_input_type::FLOAT, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		case device_id::RIGHT_GRASP_READY:
			return {WIVRN_CONTROLLER_GRASP_READY, wivrn_input_type::BOOL, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER};
		default:
			break;
	}

	throw std::range_error("bad input id " + std::string(magic_enum::enum_name(id)));
}

xrt_binding_input_pair simple_input_binding[] = {
        {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair simple_output_binding[] = {
        {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

xrt_binding_input_pair index_input_binding_left[] = {
        {XRT_INPUT_INDEX_SYSTEM_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_INDEX_SYSTEM_TOUCH, XRT_INPUT_INDEX_SYSTEM_TOUCH},
        {XRT_INPUT_INDEX_A_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_INDEX_A_TOUCH, XRT_INPUT_TOUCH_X_TOUCH},
        {XRT_INPUT_INDEX_B_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_INDEX_B_TOUCH, XRT_INPUT_TOUCH_Y_TOUCH},
        {XRT_INPUT_INDEX_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_INDEX_SQUEEZE_FORCE, XRT_INPUT_INDEX_SQUEEZE_FORCE},
        {XRT_INPUT_INDEX_TRIGGER_CLICK, XRT_INPUT_INDEX_TRIGGER_CLICK},
        {XRT_INPUT_INDEX_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_INDEX_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_INDEX_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_INDEX_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_INDEX_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_INDEX_TRACKPAD, XRT_INPUT_INDEX_TRACKPAD},
        {XRT_INPUT_INDEX_TRACKPAD_FORCE, XRT_INPUT_INDEX_TRACKPAD_FORCE},
        {XRT_INPUT_INDEX_TRACKPAD_TOUCH, XRT_INPUT_INDEX_TRACKPAD_TOUCH},
        {XRT_INPUT_INDEX_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_INDEX_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_input_pair index_input_binding_right[] = {
        {XRT_INPUT_INDEX_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_INDEX_SYSTEM_TOUCH, XRT_INPUT_INDEX_SYSTEM_TOUCH},
        {XRT_INPUT_INDEX_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_INDEX_A_TOUCH, XRT_INPUT_TOUCH_A_TOUCH},
        {XRT_INPUT_INDEX_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_INDEX_B_TOUCH, XRT_INPUT_TOUCH_B_TOUCH},
        {XRT_INPUT_INDEX_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_INDEX_SQUEEZE_FORCE, XRT_INPUT_INDEX_SQUEEZE_FORCE},
        {XRT_INPUT_INDEX_TRIGGER_CLICK, XRT_INPUT_INDEX_TRIGGER_CLICK},
        {XRT_INPUT_INDEX_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_INDEX_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_INDEX_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_INDEX_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_INDEX_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_INDEX_TRACKPAD, XRT_INPUT_INDEX_TRACKPAD},
        {XRT_INPUT_INDEX_TRACKPAD_FORCE, XRT_INPUT_INDEX_TRACKPAD_FORCE},
        {XRT_INPUT_INDEX_TRACKPAD_TOUCH, XRT_INPUT_INDEX_TRACKPAD_TOUCH},
        {XRT_INPUT_INDEX_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_INDEX_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair index_output_binding[] = {
        {XRT_OUTPUT_NAME_INDEX_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

xrt_binding_input_pair focus3_input_binding[] = {
        {XRT_INPUT_VIVE_FOCUS3_X_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_Y_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_SQUEEZE_CLICK, XRT_INPUT_VIVE_FOCUS3_SQUEEZE_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_SQUEEZE_TOUCH, XRT_INPUT_VIVE_FOCUS3_SQUEEZE_TOUCH},
        {XRT_INPUT_VIVE_FOCUS3_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_VIVE_FOCUS3_TRIGGER_CLICK, XRT_INPUT_INDEX_TRIGGER_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_VIVE_FOCUS3_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_VIVE_FOCUS3_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_VIVE_FOCUS3_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_VIVE_FOCUS3_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_VIVE_FOCUS3_THUMBREST_TOUCH, XRT_INPUT_TOUCH_THUMBREST_TOUCH},
        {XRT_INPUT_VIVE_FOCUS3_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_VIVE_FOCUS3_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair focus3_output_binding[] = {
        {XRT_OUTPUT_NAME_VIVE_FOCUS3_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

xrt_binding_input_pair touch_pro_input_binding[] = {
        {XRT_INPUT_TOUCH_PRO_X_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_TOUCH_PRO_X_TOUCH, XRT_INPUT_TOUCH_X_TOUCH},
        {XRT_INPUT_TOUCH_PRO_Y_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_TOUCH_PRO_Y_TOUCH, XRT_INPUT_TOUCH_Y_TOUCH},
        {XRT_INPUT_TOUCH_PRO_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_TOUCH_PRO_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_TOUCH_PRO_A_TOUCH, XRT_INPUT_TOUCH_A_TOUCH},
        {XRT_INPUT_TOUCH_PRO_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_TOUCH_PRO_B_TOUCH, XRT_INPUT_TOUCH_B_TOUCH},
        {XRT_INPUT_TOUCH_PRO_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_TOUCH_PRO_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_TOUCH_PRO_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_TOUCH_PRO_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_TOUCH_PRO_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_TOUCH_PRO_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_TOUCH_PRO_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_TOUCH_PRO_THUMBREST_TOUCH, XRT_INPUT_TOUCH_THUMBREST_TOUCH},
        {XRT_INPUT_TOUCH_PRO_THUMBREST_FORCE, XRT_INPUT_TOUCH_PRO_THUMBREST_FORCE},
        {XRT_INPUT_TOUCH_PRO_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_TOUCH_PRO_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_TOUCH_PRO_TRIGGER_PROXIMITY, XRT_INPUT_TOUCH_TRIGGER_PROXIMITY},
        {XRT_INPUT_TOUCH_PRO_THUMB_PROXIMITY, XRT_INPUT_TOUCH_THUMB_PROXIMITY},
        {XRT_INPUT_TOUCH_PRO_TRIGGER_CURL, XRT_INPUT_TOUCH_PRO_TRIGGER_CURL},
        {XRT_INPUT_TOUCH_PRO_TRIGGER_SLIDE, XRT_INPUT_TOUCH_PRO_TRIGGER_SLIDE},
        {XRT_INPUT_TOUCH_PRO_STYLUS_FORCE, XRT_INPUT_TOUCH_PRO_STYLUS_FORCE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair touch_pro_output_binding[] = {
        {XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
        {XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_THUMB, XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_THUMB},
        {XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_TRIGGER, XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_TRIGGER},
};

xrt_binding_input_pair touch_plus_input_binding[] = {
        {XRT_INPUT_TOUCH_PLUS_X_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_TOUCH_PLUS_X_TOUCH, XRT_INPUT_TOUCH_X_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_Y_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_TOUCH_PLUS_Y_TOUCH, XRT_INPUT_TOUCH_Y_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_TOUCH_PLUS_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_TOUCH_PLUS_A_TOUCH, XRT_INPUT_TOUCH_A_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_TOUCH_PLUS_B_TOUCH, XRT_INPUT_TOUCH_B_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_TOUCH_PLUS_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_PROXIMITY, XRT_INPUT_TOUCH_TRIGGER_PROXIMITY},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_FORCE, XRT_INPUT_TOUCH_PLUS_TRIGGER_FORCE},
        {XRT_INPUT_TOUCH_PLUS_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_TOUCH_PLUS_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_TOUCH_PLUS_THUMBREST_TOUCH, XRT_INPUT_TOUCH_THUMBREST_TOUCH},
        {XRT_INPUT_TOUCH_PLUS_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_TOUCH_PLUS_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_TOUCH_PLUS_THUMB_PROXIMITY, XRT_INPUT_TOUCH_THUMB_PROXIMITY},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_CURL, XRT_INPUT_TOUCH_PRO_TRIGGER_CURL},
        {XRT_INPUT_TOUCH_PLUS_TRIGGER_SLIDE, XRT_INPUT_TOUCH_PRO_TRIGGER_SLIDE},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair touch_plus_output_binding[] = {
        {XRT_OUTPUT_NAME_TOUCH_PLUS_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

xrt_binding_input_pair pico_neo3_input_binding[] = {
        {XRT_INPUT_PICO_NEO3_X_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_PICO_NEO3_X_TOUCH, XRT_INPUT_TOUCH_X_TOUCH},
        {XRT_INPUT_PICO_NEO3_Y_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_PICO_NEO3_Y_TOUCH, XRT_INPUT_TOUCH_Y_TOUCH},
        {XRT_INPUT_PICO_NEO3_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_PICO_NEO3_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_PICO_NEO3_TRIGGER_CLICK, XRT_INPUT_INDEX_TRIGGER_CLICK},
        {XRT_INPUT_PICO_NEO3_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_PICO_NEO3_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_PICO_NEO3_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_PICO_NEO3_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_PICO_NEO3_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_PICO_NEO3_SQUEEZE_CLICK, XRT_INPUT_VIVE_FOCUS3_SQUEEZE_CLICK},
        {XRT_INPUT_PICO_NEO3_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_PICO_NEO3_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_PICO_NEO3_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_PICO_NEO3_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_PICO_NEO3_A_TOUCH, XRT_INPUT_TOUCH_A_TOUCH},
        {XRT_INPUT_PICO_NEO3_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_PICO_NEO3_B_TOUCH, XRT_INPUT_TOUCH_B_TOUCH},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair pico_neo3_output_binding[] = {
        {XRT_OUTPUT_NAME_PICO_NEO3_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

xrt_binding_input_pair pico4_input_binding[] = {
        {XRT_INPUT_PICO4_X_CLICK, XRT_INPUT_TOUCH_X_CLICK},
        {XRT_INPUT_PICO4_X_TOUCH, XRT_INPUT_TOUCH_X_TOUCH},
        {XRT_INPUT_PICO4_Y_CLICK, XRT_INPUT_TOUCH_Y_CLICK},
        {XRT_INPUT_PICO4_Y_TOUCH, XRT_INPUT_TOUCH_Y_TOUCH},
        {XRT_INPUT_PICO4_SYSTEM_CLICK, XRT_INPUT_TOUCH_SYSTEM_CLICK},
        {XRT_INPUT_PICO4_TRIGGER_CLICK, XRT_INPUT_INDEX_TRIGGER_CLICK},
        {XRT_INPUT_PICO4_TRIGGER_VALUE, XRT_INPUT_TOUCH_TRIGGER_VALUE},
        {XRT_INPUT_PICO4_TRIGGER_TOUCH, XRT_INPUT_TOUCH_TRIGGER_TOUCH},
        {XRT_INPUT_PICO4_THUMBSTICK_CLICK, XRT_INPUT_TOUCH_THUMBSTICK_CLICK},
        {XRT_INPUT_PICO4_THUMBSTICK_TOUCH, XRT_INPUT_TOUCH_THUMBSTICK_TOUCH},
        {XRT_INPUT_PICO4_THUMBSTICK, XRT_INPUT_TOUCH_THUMBSTICK},
        {XRT_INPUT_PICO4_SQUEEZE_CLICK, XRT_INPUT_VIVE_FOCUS3_SQUEEZE_CLICK},
        {XRT_INPUT_PICO4_SQUEEZE_VALUE, XRT_INPUT_TOUCH_SQUEEZE_VALUE},
        {XRT_INPUT_PICO4_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
        {XRT_INPUT_PICO4_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
        {XRT_INPUT_PICO4_A_CLICK, XRT_INPUT_TOUCH_A_CLICK},
        {XRT_INPUT_PICO4_A_TOUCH, XRT_INPUT_TOUCH_A_TOUCH},
        {XRT_INPUT_PICO4_B_CLICK, XRT_INPUT_TOUCH_B_CLICK},
        {XRT_INPUT_PICO4_B_TOUCH, XRT_INPUT_TOUCH_B_TOUCH},
        {XRT_INPUT_PICO4_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
        {XRT_INPUT_HAND_PINCH_POSE, XRT_INPUT_HAND_PINCH_POSE},
        {XRT_INPUT_HAND_POKE_POSE, XRT_INPUT_HAND_POKE_POSE},
};

xrt_binding_output_pair pico4_output_binding[] = {
        {XRT_OUTPUT_NAME_PICO4_HAPTIC, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

template <int HandId>
constexpr auto
make_binding_profiles()
{
	constexpr auto & index_input_binding =
	        HandId == 0 ? index_input_binding_left : index_input_binding_right;
	return std::to_array<xrt_binding_profile>({
	        {
	                .name = XRT_DEVICE_SIMPLE_CONTROLLER,
	                .inputs = simple_input_binding,
	                .input_count = std::size(simple_input_binding),
	                .outputs = simple_output_binding,
	                .output_count = std::size(simple_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_INDEX_CONTROLLER,
	                .inputs = index_input_binding,
	                .input_count = std::size(index_input_binding),
	                .outputs = index_output_binding,
	                .output_count = std::size(index_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_VIVE_FOCUS3_CONTROLLER,
	                .inputs = focus3_input_binding,
	                .input_count = std::size(focus3_input_binding),
	                .outputs = focus3_output_binding,
	                .output_count = std::size(focus3_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_TOUCH_PRO_CONTROLLER,
	                .inputs = touch_pro_input_binding,
	                .input_count = std::size(touch_pro_input_binding),
	                .outputs = touch_pro_output_binding,
	                .output_count = std::size(touch_pro_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_TOUCH_PLUS_CONTROLLER,
	                .inputs = touch_plus_input_binding,
	                .input_count = std::size(touch_plus_input_binding),
	                .outputs = touch_plus_output_binding,
	                .output_count = std::size(touch_plus_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_PICO_NEO3_CONTROLLER,
	                .inputs = pico_neo3_input_binding,
	                .input_count = std::size(pico_neo3_input_binding),
	                .outputs = pico_neo3_output_binding,
	                .output_count = std::size(pico_neo3_output_binding),
	        },
	        {
	                .name = XRT_DEVICE_PICO4_CONTROLLER,
	                .inputs = pico4_input_binding,
	                .input_count = std::size(pico4_input_binding),
	                .outputs = pico4_output_binding,
	                .output_count = std::size(pico4_output_binding),
	        },
	});
}

constexpr auto controller_per_hand_binding_profiles = std::to_array({make_binding_profiles<0>(),
                                                                     make_binding_profiles<1>()});

xrt_binding_input_pair hand_interaction_simple_input_binding[] = {
        {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_HAND_PINCH_VALUE},
        {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_HAND_GRIP_POSE},
        {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_HAND_AIM_POSE},
};

xrt_binding_profile hand_interaction_binding_profiles[] = {
        {
                .name = XRT_DEVICE_SIMPLE_CONTROLLER,
                .inputs = hand_interaction_simple_input_binding,
                .input_count = std::size(hand_interaction_simple_input_binding),
                .outputs = nullptr,
                .output_count = 0,
        },
};

struct xrt_space_relation_csv_header
{};

std::ostream & operator<<(std::ostream & out, const xrt_space_relation_csv_header &)
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
	out << "qw,qx,qy,qz,";
	out << "vx,vy,vz,";
	out << "avx,avy,avz";
	return out;
}
} // namespace

wivrn_controller::wivrn_controller(xrt_device_name name,
                                   int hand_id,
                                   xrt_device * hmd,
                                   wivrn::wivrn_session * cnx) :
        xrt_device{
                .name = name,
                .device_type = hand_id == 0 ? XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER : XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER,
                .hmd = nullptr,
                .tracking_origin = hmd->tracking_origin,
                .binding_profile_count = name != XRT_DEVICE_EXT_HAND_INTERACTION ? controller_per_hand_binding_profiles[hand_id].size() : std::size(hand_interaction_binding_profiles),
                .binding_profiles = name != XRT_DEVICE_EXT_HAND_INTERACTION ? (struct xrt_binding_profile *)controller_per_hand_binding_profiles[hand_id].data() : hand_interaction_binding_profiles,
                .input_count = WIVRN_CONTROLLER_INPUT_COUNT,
                .supported = {
                        .orientation_tracking = true,
                        .position_tracking = true,
                        .hand_tracking = name != XRT_DEVICE_EXT_HAND_INTERACTION and cnx->get_info().hand_tracking,
                },
                .update_inputs = method_pointer<&wivrn_controller::update_inputs>,
                .get_tracked_pose = method_pointer<&wivrn_controller::get_tracked_pose>,
                .get_hand_tracking = method_pointer<&wivrn_controller::get_hand_tracking>,
                .set_output = method_pointer<&wivrn_controller::set_output>,
                .destroy = [](xrt_device *) {},
        },
        grip(hand_id == 0 ? device_id::LEFT_GRIP : device_id::RIGHT_GRIP),
        aim(hand_id == 0 ? device_id::LEFT_AIM : device_id::RIGHT_AIM),
        palm(hand_id == 0 ? device_id::LEFT_PALM : device_id::RIGHT_PALM),
        pinch_ext(hand_id == 0 ? device_id::LEFT_PINCH_POSE : device_id::RIGHT_PINCH_POSE),
        poke_ext(hand_id == 0 ? device_id::LEFT_POKE : device_id::RIGHT_POKE),
        joints(hand_id),
        inputs_array(input_count, xrt_input{}),
        cnx(cnx)
{
	inputs = inputs_array.data();

	// Setup input.
#define SET_INPUT(VENDOR, NAME)                                                     \
	do                                                                          \
	{                                                                           \
		inputs[WIVRN_CONTROLLER_##NAME].name = XRT_INPUT_##VENDOR##_##NAME; \
		inputs[WIVRN_CONTROLLER_##NAME].active = true;                      \
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

	if (name != XRT_DEVICE_EXT_HAND_INTERACTION)
	{
		SET_INPUT(TOUCH, AIM_POSE);
		SET_INPUT(TOUCH, GRIP_POSE);
		SET_INPUT(GENERIC, PALM_POSE);
		SET_INPUT(HAND, PINCH_POSE);
		SET_INPUT(HAND, POKE_POSE);
		if (hand_id == 0)
		{
			inputs[WIVRN_CONTROLLER_HAND_TRACKING_LEFT].name = XRT_INPUT_HT_UNOBSTRUCTED_LEFT;
			inputs[WIVRN_CONTROLLER_HAND_TRACKING_LEFT].active = true;
			SET_INPUT(TOUCH, MENU_CLICK);
			SET_INPUT(TOUCH, X_CLICK);
			SET_INPUT(TOUCH, Y_CLICK);
			SET_INPUT(TOUCH, X_TOUCH);
			SET_INPUT(TOUCH, Y_TOUCH);
		}
		else
		{
			inputs[WIVRN_CONTROLLER_HAND_TRACKING_RIGHT].name = XRT_INPUT_HT_UNOBSTRUCTED_RIGHT;
			inputs[WIVRN_CONTROLLER_HAND_TRACKING_RIGHT].active = true;
			SET_INPUT(TOUCH, SYSTEM_CLICK);
			SET_INPUT(TOUCH, A_CLICK);
			SET_INPUT(TOUCH, B_CLICK);
			SET_INPUT(TOUCH, A_TOUCH);
			SET_INPUT(TOUCH, B_TOUCH);
		}
		SET_INPUT(VIVE_FOCUS3, SQUEEZE_CLICK);
		SET_INPUT(INDEX, SQUEEZE_FORCE);
		SET_INPUT(TOUCH, SQUEEZE_VALUE);
		SET_INPUT(INDEX, TRIGGER_CLICK);
		SET_INPUT(TOUCH, TRIGGER_VALUE);
		SET_INPUT(TOUCH, TRIGGER_TOUCH);
		SET_INPUT(TOUCH, TRIGGER_PROXIMITY);
		SET_INPUT(TOUCH_PRO, TRIGGER_CURL);
		SET_INPUT(TOUCH_PRO, TRIGGER_SLIDE);
		SET_INPUT(TOUCH_PLUS, TRIGGER_FORCE);
		SET_INPUT(TOUCH, THUMBSTICK);
		SET_INPUT(TOUCH, THUMBSTICK_CLICK);
		SET_INPUT(TOUCH, THUMBSTICK_TOUCH);
		SET_INPUT(TOUCH, THUMBREST_TOUCH);
		SET_INPUT(TOUCH_PRO, THUMBREST_FORCE);
		SET_INPUT(TOUCH, THUMB_PROXIMITY);
		SET_INPUT(INDEX, TRACKPAD);
		SET_INPUT(VIVE, TRACKPAD_CLICK);
		SET_INPUT(INDEX, TRACKPAD_FORCE);
		SET_INPUT(INDEX, TRACKPAD_TOUCH);
		SET_INPUT(TOUCH_PRO, STYLUS_FORCE);

		outputs_array = {
		        {.name = XRT_OUTPUT_NAME_TOUCH_HAPTIC},
		        {.name = XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_THUMB},
		        {.name = XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_TRIGGER},
		};

		output_count = outputs_array.size();
		outputs = outputs_array.data();
	}
	else
	{
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
	}

#undef SET_INPUT
	inputs_staging = inputs_array;

	auto name_str = std::format("WiVRn {} {}", hand_id == 0 ? "left" : "right", name != XRT_DEVICE_EXT_HAND_INTERACTION ? "controller" : "hand interaction");
	// Print name.
	strcpy(str, name_str.c_str());
	strcpy(serial, name_str.c_str());
}

xrt_result_t wivrn_controller::update_inputs()
{
	std::lock_guard _{mutex};
	inputs_array = inputs_staging;
	return XRT_SUCCESS;
}

void wivrn_controller::set_inputs(const from_headset::inputs & inputs, const clock_offset & clock_offset)
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

xrt_result_t wivrn_controller::get_tracked_pose(xrt_input_name name, int64_t at_timestamp_ns, xrt_space_relation * res)
{
	std::chrono::nanoseconds extrapolation_time;
	device_id device;
	switch (name)
	{
		case XRT_INPUT_TOUCH_AIM_POSE:
		case XRT_INPUT_HAND_AIM_POSE:
			std::tie(extrapolation_time, *res, device) = aim.get_pose_at(at_timestamp_ns);
			break;
		case XRT_INPUT_TOUCH_GRIP_POSE:
		case XRT_INPUT_HAND_GRIP_POSE:
			std::tie(extrapolation_time, *res, device) = grip.get_pose_at(at_timestamp_ns);
			break;
		case XRT_INPUT_GENERIC_PALM_POSE:
			std::tie(extrapolation_time, *res, device) = palm.get_pose_at(at_timestamp_ns);
			break;
		case XRT_INPUT_HAND_PINCH_POSE:
			std::tie(extrapolation_time, *res, device) = pinch_ext.get_pose_at(at_timestamp_ns);
			break;
		case XRT_INPUT_HAND_POKE_POSE:
			std::tie(extrapolation_time, *res, device) = poke_ext.get_pose_at(at_timestamp_ns);
			break;
		default:
			U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
			return XRT_ERROR_INPUT_UNSUPPORTED;
	}
	cnx->set_enabled(device, true);
	cnx->add_predict_offset(extrapolation_time);
	if (auto out = tracking_dump())
	{
		auto device = [&] {
		switch (name)
		{
			case XRT_INPUT_TOUCH_AIM_POSE: return aim.device;
			case XRT_INPUT_TOUCH_GRIP_POSE: return grip.device;
			case XRT_INPUT_GENERIC_PALM_POSE: return palm.device;
			case XRT_INPUT_HAND_PINCH_POSE: return pinch_ext.device;
			case XRT_INPUT_HAND_POKE_POSE: return poke_ext.device;
			default:
				assert(false);
				__builtin_unreachable();
		} }();
		*out->lock() << magic_enum::enum_name(device) << ','
		             << os_monotonic_get_ns() << ','
		             << at_timestamp_ns << ','
		             << extrapolation_time.count() << ','
		             << "g,"
		             << *res << std::endl;
		;
	}
	return XRT_SUCCESS;
}

xrt_result_t wivrn_controller::get_hand_tracking(xrt_input_name name, int64_t desired_timestamp_ns, xrt_hand_joint_set * out_value, int64_t * out_timestamp_ns)
{
	switch (name)
	{
		case XRT_INPUT_HT_UNOBSTRUCTED_LEFT:
		case XRT_INPUT_HT_UNOBSTRUCTED_RIGHT: {
			*out_timestamp_ns = desired_timestamp_ns;
			std::chrono::nanoseconds extrapolation_time;
			std::tie(extrapolation_time, *out_value) = joints.get_at(desired_timestamp_ns);
			cnx->add_predict_offset(extrapolation_time);
			cnx->set_enabled(joints.hand_id == 0 ? to_headset::tracking_control::id::left_hand : to_headset::tracking_control::id::right_hand, true);
			return XRT_SUCCESS;
		}

		default:
			U_LOG_XDEV_UNSUPPORTED_INPUT(this, u_log_get_global_level(), name);
			return XRT_ERROR_INPUT_UNSUPPORTED;
	}
}

void wivrn_controller::set_derived_pose(const from_headset::derived_pose & derived)
{
	auto list = [this](device_id id) -> pose_list * {
		for (auto item: {&grip, &aim, &palm, &pinch_ext, &poke_ext})
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
	if (not pinch_ext.update_tracking(tracking, offset))
		cnx->set_enabled(pinch_ext.device, false);
	if (not poke_ext.update_tracking(tracking, offset))
		cnx->set_enabled(poke_ext.device, false);
	if (auto out = tracking_dump(); out and offset)
	{
		auto locked = out->lock();
		auto now = os_monotonic_get_ns();
		for (const auto & pose: tracking.device_poses)
		{
			if (pose.device == aim.device or pose.device == grip.device or pose.device == palm.device or pose.device == pinch_ext.device or pose.device == poke_ext.device)
				*locked << magic_enum::enum_name(pose.device) << ','
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

xrt_result_t wivrn_controller::set_output(xrt_output_name name, const xrt_output_value * value)
{
	device_id id;
	const bool left = device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
	switch (name)
	{
		case XRT_OUTPUT_NAME_TOUCH_HAPTIC:
			id = left ? device_id::LEFT_CONTROLLER_HAPTIC : device_id::RIGHT_CONTROLLER_HAPTIC;
			break;
		case XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_TRIGGER:
			id = left ? device_id::LEFT_TRIGGER_HAPTIC : device_id::RIGHT_TRIGGER_HAPTIC;
			break;
		case XRT_OUTPUT_NAME_TOUCH_PRO_HAPTIC_THUMB:
			id = left ? device_id::LEFT_THUMB_HAPTIC : device_id::RIGHT_THUMB_HAPTIC;
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

void wivrn_controller::reset_history()
{
	grip.reset();
	aim.reset();
	palm.reset();
	pinch_ext.reset();
	poke_ext.reset();
}

thread_safe<std::ofstream> * wivrn_controller::tracking_dump()
{
	static std::unique_ptr<thread_safe<std::ofstream>> res = [] {
		if (auto wivrn_dump = std::getenv("WIVRN_DUMP_TRACKING"))
		{
			U_LOG_I("Controller tracking dump enabled: %s", wivrn_dump);
			std::ofstream res;
			res.open(wivrn_dump);
			res << "device_id,"
			       "now_ns,"
			       "timestamp_ns,"
			       "extrapolation_ns,"
			       "receive/get,"
			    << xrt_space_relation_csv_header{} << std::endl;

			return std::make_unique<thread_safe<std::ofstream>>(std::move(res));
		}
		return std::unique_ptr<thread_safe<std::ofstream>>();
	}();
	return res.get();
}
} // namespace wivrn

std::ostream & operator<<(std::ostream & out, const xrt_space_relation & rel)
{
	const auto & pos = rel.pose.position;
	const auto & o = rel.pose.orientation;
	const auto & v = rel.linear_velocity;
	const auto & av = rel.angular_velocity;
	for (const auto & [value, name]: magic_enum::enum_entries<xrt_space_relation_flags>())
	{
		if (value and value != XRT_SPACE_RELATION_BITMASK_ALL)
			out << bool(rel.relation_flags & value) << ',';
	}
	out << pos.x << ',' << pos.y << ',' << pos.z << ',';
	out << o.w << ',' << o.x << ',' << o.y << ',' << o.z << ',';
	out << v.x << ',' << v.y << ',' << v.z << ',';
	out << av.x << ',' << av.y << ',' << av.z;
	return out;
}
