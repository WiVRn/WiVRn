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

#pragma once

#include "imgui.h"
#include <string_view>
#include <vector>

struct ImRect;

class virtual_keyboard
{
public:
	enum key_flags
	{
		key_flag_none = 0,
		key_flag_hidden = 1 << 0,
		key_flag_repeat = 1 << 1,
	};

	struct key
	{
		float width;
		const std::u16string_view characters;
		ImGuiKey key = ImGuiKey_None;
		const char * glyph = nullptr;
		key_flags flag = key_flag_none;
	};

	using layout = std::vector<std::vector<key>>;

	enum class case_mode
	{
		lower,
		upper,
		caps_lock
	};

private:
	bool button_behavior(const ImRect & bb, ImGuiID id, bool * out_hovered, bool * out_held, ImGuiButtonFlags flags = 0);
	bool draw_single_key(const key & k, int key_id, ImVec2 size_arg, bool & hovered);
	void press_single_key(const key & k);

	ImGuiID active_id = 0;
	float held_duration = 0;

	case_mode current_case_mode = case_mode::lower;
	int current_layout = 0;
	bool symbols_shown = false;

public:
	void display(ImGuiID & hovered);
};
