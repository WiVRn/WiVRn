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
#include "render/imgui_impl.h"
#include <string>
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
		std::string characters_lower;
		std::string characters_upper;
		ImGuiKey keycode = ImGuiKey_None;
		std::string glyph_lower;
		std::string glyph_upper;
		key_flags flag = key_flag_none;

		key(float width, std::string_view characters, ImGuiKey keycode = ImGuiKey_None, key_flags flag = key_flag_none);
		key(float width);
	};

	using layout = std::vector<std::vector<key>>;

private:
	enum class case_mode
	{
		lower,
		upper,
		caps_lock
	};

	struct key_status
	{
		bool hovered = false;
		bool held = false;
		bool pressed = false;
		float hold_duration = 0;
	};

	key_status button_behavior(const ImRect & bb, ImGuiID id, bool window_hovered, ImGuiButtonFlags flags = 0);
	key_status draw_single_key(const key & k, int key_id, ImVec2 size_arg, bool window_hovered);
	void press_single_key(const key & k);
	bool is_window_hovered();

	ImGuiID active_id = 0;
	float held_duration = 0;

	case_mode current_case_mode = case_mode::lower;
	int current_layout = 0;
	bool symbols_shown = false;

public:
	void display(imgui_context & ctx);
	const std::string_view get_layout() const;
	void set_layout(std::string_view layout_name);
};
