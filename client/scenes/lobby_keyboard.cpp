/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#define IMGUI_DEFINE_MATH_OPERATORS

#include <array>
#include <cassert>
#include <cctype>
#include <limits>
#include <spdlog/spdlog.h>
#include <string_view>
#include <uni_algo/case.h>
#include <uni_algo/ranges_grapheme.h>
#include <utility>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "lobby_keyboard.h"

static const ImGuiKey key_layout = (ImGuiKey)(ImGuiKey_NamedKey_END + 1);
static const ImGuiKey key_symbols_letter = (ImGuiKey)(ImGuiKey_NamedKey_END + 2);

// See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fallback/main.qml
virtual_keyboard::layout qwerty = {
        {
                {0.5},
                {1, "q"},
                {1, "w"},
                {1, "eéèêë"},
                {1, "rŕrř"},
                {1, "tţtŧť"},
                {1, "yÿyýŷ"},
                {1, "uűūũûüuùú"},
                {1, "iîïīĩiìí"},
                {1, "oœøõôöòó"},
                {1, "p"},
                {1.5, ICON_FA_DELETE_LEFT, ImGuiKey_Backspace, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1},
                {1, "aaäåãâàá"},
                {1, "sšsşś"},
                {1, "ddđď"},
                {1, "f"},
                {1, "gġgģĝğ"},
                {1, "h"},
                {1, "j"},
                {1, "k"},
                {1, "lĺŀłļľl"},
                {2},
        }, // 12
        {
                {1.5, "", ImGuiKey_LeftShift},
                {1, "zzžż"},
                {1, "x"},
                {1, "cçcċčć"},
                {1, "v"},
                {1, "b"},
                {1, "nņńnň"},
                {1, "m"},
                {1, ","},
                {1, "."},
                // {1, u"-"},
                {1.5, "", ImGuiKey_RightShift},
        }, // 12
        {
                {2},
                {1, "?123", key_symbols_letter},
                {1, ICON_FA_GLOBE, key_layout},
                {5, " "},
                {3},
        } // 12
};

// See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fr_FR/main.qml
virtual_keyboard::layout azerty = {
        {
                {0.5},
                {1, "aàâæ"},
                {1, "z"},
                {1, "eéèêë"},
                {1, "r"},
                {1, "t"},
                {1, "yÿ"},
                {1, "uùûü"},
                {1, "iîï"},
                {1, "oôœ"},
                {1, "p"},
                {1.5, ICON_FA_DELETE_LEFT, ImGuiKey_Backspace, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1},
                {1, "q"},
                {1, "s"},
                {1, "d"},
                {1, "f"},
                {1, "g"},
                {1, "h"},
                {1, "j"},
                {1, "k"},
                {1, "l"},
                {1, "m"},
                // {1, "é"},
                {1},
        }, // 12
        {
                {1.5, "", ImGuiKey_LeftShift},
                {1, "w"},
                {1, "x"},
                {1, "cç"},
                {1, "v"},
                {1, "b"},
                {1, "n"},
                {1, ","},
                {1, "."},
                {1, "-"},
                {1.5, "", ImGuiKey_RightShift},
        }, // 12
        {
                {2},
                {1, "?123", key_symbols_letter},
                {1, ICON_FA_GLOBE, key_layout},
                {5, " "},
                {3},
        } // 12
};

virtual_keyboard::layout digits = {
        {
                {1, "1"},
                {1, "2"},
                {1, "3"},
        }, // 3
        {
                {1, "4"},
                {1, "5"},
                {1, "6"},
        }, // 3
        {
                {1, "7"},
                {1, "8"},
                {1, "9"},
        }, // 3
        {
                {2, "0"},
                {1, ICON_FA_DELETE_LEFT, ImGuiKey_Backspace, virtual_keyboard::key_flag_repeat},
        } // 3
};

// // See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fallback/main.qml
// virtual_keyboard::layout qwertz = {
// };

virtual_keyboard::layout symbols = {
        {
                {0.5},
                {1, "1"},
                {1, "2"},
                {1, "3"},
                {1, "4"},
                {1, "5"},
                {1, "6"},
                {1, "7"},
                {1, "8"},
                {1, "9"},
                {1, "0"},
                {1.5, ICON_FA_DELETE_LEFT, ImGuiKey_Backspace, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1},
                {1, "@"},
                {1, "#"},
                {1, "%"},
                {1, "&"},
                {1, "*"},
                {1, "_"},
                {1, "-"},
                {1, "+"},
                {1, "("},
                {1, ")"},
                {1},
        }, // 12
        {
                {1.5},
                {1}, // {1, "", ImGuiKey_None, "1/2", virtual_keyboard::key_flag_hidden},
                {1, "\""},
                {1, "<"},
                {1, ">"},
                {1, "'"},
                {1, ":"},
                {1, "/"},
                {1, "!"},
                {1, "?"},
                {1.5},
        }, // 12
        {
                {1, "ABC", key_symbols_letter},
                {1, ICON_FA_GLOBE, key_layout},
                {5, " "},
                {3},
        },
};

std::array layouts = {
        std::make_pair<std::string_view, virtual_keyboard::layout *>("QWERTY", &qwerty),
        std::make_pair<std::string_view, virtual_keyboard::layout *>("AZERTY", &azerty),
};

virtual_keyboard::key::key(float width, std::string_view characters, ImGuiKey keycode, key_flags flag) :
        width(width),
        keycode(keycode),
        flag(flag)
{
	if (keycode == ImGuiKey_None)
	{
		// Normal key: the first character is displayed on the key
		characters_lower = characters;
		characters_upper = una::cases::to_uppercase_utf8(characters);

		glyph_lower = *una::ranges::views::grapheme::utf8(characters_lower).begin();
		glyph_upper = *una::ranges::views::grapheme::utf8(characters_upper).begin();
	}
	else
	{
		// Special key: characters contains the key label, except shift which depends
		// on caps lock
		assert(not characters.empty() or keycode == ImGuiKey_LeftShift or keycode == ImGuiKey_RightShift);
		glyph_lower = (std::string)characters;
		glyph_upper = (std::string)characters;
	}
}

virtual_keyboard::key::key(float width) :
        width(width),
        keycode(ImGuiKey_None),
        flag(virtual_keyboard::key_flag_hidden)
{
}

// Reimplemented because ImGui::IsWindowHovered() returns false if a popup is open
bool virtual_keyboard::is_window_hovered()
{
	int i = 0;
	ImGuiContext & g = *GImGui;

	if (not ImGui::IsMouseHoveringRect(g.CurrentWindow->Pos, g.CurrentWindow->Pos + g.CurrentWindow->Size))
		return false;

	// Ignore any window below the keyboard
	while (g.Windows[i] != g.CurrentWindow and i < g.Windows.Size)
		i++;

	// Cannot find the keyboard window
	if (i >= g.Windows.Size)
		return false;

	// Skip the keyboard window
	i++;

	// Check if another window is over the keyboard
	while (i < g.Windows.Size)
	{
		if (ImGui::IsMouseHoveringRect(g.Windows[i]->Pos, g.Windows[i]->Pos + g.Windows[i]->Size))
			return false;

		i++;
	}

	return true;
}

// Mostly copied from ImGui::ButtonBehavior, massively simplified for the keyboard use case, don't take focus when clicked
virtual_keyboard::key_status virtual_keyboard::button_behavior(const ImRect & bb, ImGuiID id, bool window_hovered, ImGuiButtonFlags flags)
{
	ImGuiContext & g = *GImGui;

	if ((flags & ImGuiButtonFlags_PressedOnMask_) == 0)
		flags |= ImGuiButtonFlags_PressedOnClickRelease;

	bool pressed = false;
	bool hovered = window_hovered and ImGui::IsMouseHoveringRect(bb.Min, bb.Max);

	bool mouse_down = ImGui::IsMouseDown(0, id);

	// Mouse handling
	if (hovered and mouse_down and active_id != id)
	{
		active_id = id;
		held_duration = 0;

		if (flags & ImGuiButtonFlags_PressedOnClick)
			pressed = true;
	}

	// Process while held
	bool held = false;
	if (active_id == id)
	{
		if (mouse_down)
		{
			held = true;

			float prev_held_duration = held_duration;
			held_duration += g.IO.DeltaTime;
			// if ((flags & ImGuiButtonFlags_Repeat) and held_duration > g.IO.KeyRepeatDelay)
			// {
			// 	int n1 = (prev_held_duration - g.IO.KeyRepeatDelay) / g.IO.KeyRepeatRate;
			// 	int n2 = (held_duration - g.IO.KeyRepeatDelay) / g.IO.KeyRepeatRate;
			//
			// 	if (n2 != n1)
			// 		pressed = true;
			// }
		}
		else
		{
			if (hovered and (flags & ImGuiButtonFlags_PressedOnClickRelease))
				pressed = true;

			active_id = 0;
		}
	}

	return key_status{
	        .hovered = hovered,
	        .held = held,
	        .pressed = pressed,
	        .hold_duration = held ? held_duration : 0};
}

virtual_keyboard::key_status virtual_keyboard::draw_single_key(const key & k, int key_id, ImVec2 size_arg, bool window_hovered)
{
	bool is_shift = k.keycode == ImGuiKey_LeftShift or k.keycode == ImGuiKey_RightShift;

	const std::string & key_label = is_shift
	                                        ? (current_case_mode == case_mode::lower)
	                                                  ? ICON_FA_CHEVRON_UP
	                                                  : ICON_FA_CIRCLE_CHEVRON_UP
	                                        : ((current_case_mode == case_mode::lower)
	                                                   ? k.glyph_lower
	                                                   : k.glyph_upper);

	std::string key_label_id;
	if (k.keycode != ImGuiKey_None)
		key_label_id = "virtual_keyboard_key_" + std::to_string(k.keycode);
	else
		key_label_id = "virtual_keyboard_" + std::string(k.glyph_lower);

	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return {};

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(key_label_id.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(key_label.c_str(), key_label.c_str() + key_label.size(), false /* Don't hide text after # */);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return {};

	ImGuiButtonFlags flags = ImGuiButtonFlags_None;

	// if (k.flag & key_flag_repeat)
	// 	 flags |= ImGuiButtonFlags_Repeat;

	auto status = button_behavior(bb, id, window_hovered, flags);

	// Render
	bool active = (status.held and status.hovered) or (is_shift and current_case_mode == case_mode::caps_lock);

	const ImU32 col = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : status.hovered ? ImGuiCol_ButtonHovered
	                                                                                     : ImGuiCol_Button);
	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

	ImGui::RenderTextClippedEx(
	        g.CurrentWindow->DrawList,
	        bb.Min + style.FramePadding,
	        bb.Max - style.FramePadding,
	        key_label.c_str(),
	        key_label.c_str() + key_label.size(),
	        &label_size,
	        style.ButtonTextAlign,
	        &bb);

	if (status.pressed)
		press_single_key(k);

	return status;
}

void virtual_keyboard::press_single_key(const key & k)
{
	switch ((int)k.keycode) // cast to int to avoid -Werror=switch
	{
		case ImGuiKey_LeftShift:
		case ImGuiKey_RightShift:
			switch (current_case_mode)
			{
				case case_mode::lower:
					current_case_mode = case_mode::upper;
					break;
				case case_mode::upper:
					current_case_mode = case_mode::caps_lock;
					break;
				case case_mode::caps_lock:
					current_case_mode = case_mode::lower;
					break;
			}
			break;

		case key_layout:
			current_layout = (current_layout + 1) % layouts.size();
			break;

		case key_symbols_letter:
			symbols_shown = not symbols_shown;
			break;

		case ImGuiKey_None:
			switch (current_case_mode)
			{
				case case_mode::lower:
					ImGui::GetIO().AddInputCharactersUTF8(std::string(*una::ranges::views::grapheme::utf8(k.characters_lower).begin()).c_str());
					break;

				case case_mode::upper:
					current_case_mode = case_mode::lower;
					[[fallthrough]];

				case case_mode::caps_lock:
					ImGui::GetIO().AddInputCharactersUTF8(std::string(*una::ranges::views::grapheme::utf8(k.characters_upper).begin()).c_str());
					break;
			}
			break;

		default:
			ImGui::GetIO().AddKeyEvent(k.keycode, true);
			ImGui::GetIO().AddKeyEvent(k.keycode, false);
			break;
	}
}

void virtual_keyboard::display(imgui_context & ctx)
{
	ImGuiStyle & style = ImGui::GetStyle();

	ImGuiInputTextState * input_state = ImGui::GetInputTextState(ImGui::GetCurrentContext()->ActiveId);
	bool want_digits = input_state and (input_state->Flags & ImGuiInputTextFlags_CharsDecimal) != 0;

	auto & layout = want_digits ? digits : symbols_shown ? symbols
	                                                     : *layouts[current_layout].second;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 8});
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 8, 224));
	if (want_digits)
		ImGui::SetNextWindowSize({350, 400});
	else
		ImGui::SetNextWindowSize({1400, 400});

	ImGui::Begin("VirtualKeyboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoFocusOnClick);

	ImVec2 size = ImGui::GetWindowSize();
	ImVec2 padding = ImGui::GetStyle().WindowPadding;
	size = {size.x - 2 * padding.x, size.y - 2 * padding.y};

	// Compute keys size:
	float key_height = [&]() {
		int N = layout.size();

		// size.y == N * key_height + (N - 1) * ItemSpacing.y
		return (size.y - (N - 1) * style.ItemSpacing.y) / N;
	}();

	float base_key_width = std::numeric_limits<float>::max();
	for (const auto & row: layout)
	{
		// key.width is scaled by base_key_width to get the real width of the key:
		// key_width = base_key_width * key.width - ItemSpacing.x

		// size.x == sum(key_width) + (N - 1) * ItemSpacing.x
		//        == base_key_width * sum(key.width) - ItemSpacing.x

		float total_width = 0;
		for (auto & key: row)
			total_width += key.width;

		base_key_width = std::min(base_key_width, (size.x + style.ItemSpacing.x) / total_width);
	}

	ImVec2 row_position = ImGui::GetCursorPos();
	int id = 0;

	bool window_hovered = is_window_hovered();

	for (const auto & row: layout)
	{
		ImVec2 key_position = row_position;

		// Align right
		key_position.x += size.x + style.ItemSpacing.x;
		for (auto & key: row)
			key_position.x -= base_key_width * key.width;

		for (const auto & key: row)
		{
			if (!(key.flag & key_flag_hidden))
			{
				ImGui::SetCursorPos(key_position);

				auto status = draw_single_key(key, id++, ImVec2{base_key_width * key.width - style.ItemSpacing.x, key_height}, window_hovered);
				if (status.hovered)
					ctx.set_hovered_item();
			}

			key_position.x += base_key_width * key.width;
		}

		row_position.y += key_height + style.ItemSpacing.y;
	}

	row_position.y -= style.ItemSpacing.y;

	ImGui::SetCursorPos(row_position);

	if (is_window_hovered())
	{
		ImGui::GetIO().MouseDown[0] = false;
		ImGui::GetIO().MouseClicked[0] = false;
	}

	ImGui::End();
	ImGui::PopStyleColor(); // ImGuiCol_WindowBg
	ImGui::PopStyleVar(4);  // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_WindowRounding, ImGuiStyleVar_ItemSpacing, ImGuiStyleVar_FrameRounding
}

const std::string_view virtual_keyboard::get_layout() const
{
	return layouts[current_layout].first;
}

void virtual_keyboard::set_layout(std::string_view layout_name)
{
	for (int i = 0, n = layouts.size(); i < n; ++i)
	{
		if (layouts[i].first == layout_name)
		{
			current_layout = i;
			return;
		}
	}
}
