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
};

inline gamepad_input map_gamepad(device_id id)
{
	switch (id)
	{
		case device_id::GAMEPAD_MENU_CLICK:
			return {gp_menu, gp_value::button};
		case device_id::GAMEPAD_VIEW_CLICK:
			return {gp_view, gp_value::button};
		case device_id::GAMEPAD_A_CLICK:
			return {gp_a, gp_value::button};
		case device_id::GAMEPAD_B_CLICK:
			return {gp_b, gp_value::button};
		case device_id::GAMEPAD_X_CLICK:
			return {gp_x, gp_value::button};
		case device_id::GAMEPAD_Y_CLICK:
			return {gp_y, gp_value::button};
		case device_id::GAMEPAD_SHOULDER_LEFT_CLICK:
			return {gp_shoulder_left, gp_value::button};
		case device_id::GAMEPAD_SHOULDER_RIGHT_CLICK:
			return {gp_shoulder_right, gp_value::button};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_CLICK:
			return {gp_thumbstick_left_click, gp_value::button};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_CLICK:
			return {gp_thumbstick_right_click, gp_value::button};
		case device_id::GAMEPAD_DPAD_DOWN_CLICK:
			return {gp_dpad_down, gp_value::dpad};
		case device_id::GAMEPAD_DPAD_RIGHT_CLICK:
			return {gp_dpad_right, gp_value::dpad};
		case device_id::GAMEPAD_DPAD_UP_CLICK:
			return {gp_dpad_up, gp_value::dpad};
		case device_id::GAMEPAD_DPAD_LEFT_CLICK:
			return {gp_dpad_left, gp_value::dpad};
		case device_id::GAMEPAD_TRIGGER_LEFT_VALUE:
			return {gp_trigger_left, gp_value::trigger};
		case device_id::GAMEPAD_TRIGGER_RIGHT_VALUE:
			return {gp_trigger_right, gp_value::trigger};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_X:
			return {gp_thumbstick_left, gp_value::stick_x};
		case device_id::GAMEPAD_THUMBSTICK_LEFT_Y:
			return {gp_thumbstick_left, gp_value::stick_y};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_X:
			return {gp_thumbstick_right, gp_value::stick_x};
		case device_id::GAMEPAD_THUMBSTICK_RIGHT_Y:
			return {gp_thumbstick_right, gp_value::stick_y};
		default:
			return {gp_count, gp_value::button};
	}
}

} // namespace wivrn
