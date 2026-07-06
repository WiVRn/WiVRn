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

#pragma once

#include "wivrn_packets.h"

#include <cstdint>
#include <linux/input-event-codes.h>

namespace wivrn
{
enum gp_input
{
	gp_menu,
	gp_view,
	gp_a,
	gp_b,
	gp_x,
	gp_y,
	gp_dpad_down,
	gp_dpad_right,
	gp_dpad_up,
	gp_dpad_left,
	gp_shoulder_left,
	gp_shoulder_right,
	gp_thumbstick_left_click,
	gp_thumbstick_right_click,
	gp_thumbstick_left,
	gp_thumbstick_right,
	gp_trigger_left,
	gp_trigger_right,
	gp_count,
};

enum class gp_value
{
	button,
	dpad,
	trigger,
	stick_x,
	stick_y,
};

struct gamepad_input
{
	gp_input slot;
	gp_value kind;
	uint16_t ev_code; // BTN_* or ABS_*, unused for dpad
};

inline gamepad_input map_gamepad(device_id id)
{
	switch (id)
	{
		case device_id::GAMEPAD_MENU_CLICK:
			return {gp_menu, gp_value::button, BTN_START};
		case device_id::GAMEPAD_VIEW_CLICK:
			return {gp_view, gp_value::button, BTN_SELECT};
		case device_id::GAMEPAD_A_CLICK:
			return {gp_a, gp_value::button, BTN_SOUTH};
		case device_id::GAMEPAD_B_CLICK:
			return {gp_b, gp_value::button, BTN_EAST};
		// BTN_X/BTN_Y aliases don't match compass positions (BTN_X == BTN_NORTH,
		// BTN_Y == BTN_WEST); xpad and SDL expect the aliases, not the positions
		case device_id::GAMEPAD_X_CLICK:
			return {gp_x, gp_value::button, BTN_X};
		case device_id::GAMEPAD_Y_CLICK:
			return {gp_y, gp_value::button, BTN_Y};
		case device_id::GAMEPAD_SHOULDER_LEFT_CLICK:
			return {gp_shoulder_left, gp_value::button, BTN_TL};
		case device_id::GAMEPAD_SHOULDER_RIGHT_CLICK:
			return {gp_shoulder_right, gp_value::button, BTN_TR};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_CLICK:
			return {gp_thumbstick_left_click, gp_value::button, BTN_THUMBL};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_CLICK:
			return {gp_thumbstick_right_click, gp_value::button, BTN_THUMBR};
		case device_id::GAMEPAD_DPAD_DOWN_CLICK:
			return {gp_dpad_down, gp_value::dpad, 0};
		case device_id::GAMEPAD_DPAD_RIGHT_CLICK:
			return {gp_dpad_right, gp_value::dpad, 0};
		case device_id::GAMEPAD_DPAD_UP_CLICK:
			return {gp_dpad_up, gp_value::dpad, 0};
		case device_id::GAMEPAD_DPAD_LEFT_CLICK:
			return {gp_dpad_left, gp_value::dpad, 0};
		case device_id::GAMEPAD_TRIGGER_LEFT_VALUE:
			return {gp_trigger_left, gp_value::trigger, ABS_Z};
		case device_id::GAMEPAD_TRIGGER_RIGHT_VALUE:
			return {gp_trigger_right, gp_value::trigger, ABS_RZ};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_X:
			return {gp_thumbstick_left, gp_value::stick_x, ABS_X};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_Y:
			return {gp_thumbstick_left, gp_value::stick_y, ABS_Y};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_X:
			return {gp_thumbstick_right, gp_value::stick_x, ABS_RX};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_Y:
			return {gp_thumbstick_right, gp_value::stick_y, ABS_RY};
		default:
			return {gp_count, gp_value::button, 0};
	}
}

} // namespace wivrn
