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

#include <cctype>
#include <limits>
#include <spdlog/spdlog.h>
#include <string_view>
#include <unicode/uchar.h>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "lobby_keyboard.h"

// #include <stdexcept>
// #include <cwchar>
// #include <ranges>
// #include <unicode/ustring.h> // u_strToUpper

static const ImGuiKey key_layout = (ImGuiKey)(ImGuiKey_NamedKey_END + 1);
static const ImGuiKey key_symbols_letter = (ImGuiKey)(ImGuiKey_NamedKey_END + 2);

// See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fallback/main.qml
virtual_keyboard::layout qwerty = {
        {
                {0.5, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"q"},
                {1, u"w"},
                {1, u"eéèêë"},
                {1, u"rŕrř"},
                {1, u"tţtŧť"},
                {1, u"yÿyýŷ"},
                {1, u"uűūũûüuùú"},
                {1, u"iîïīĩiìí"},
                {1, u"oœøõôöòó"},
                {1, u"p"},
                {1.5, u"", ImGuiKey_Backspace, ICON_FA_DELETE_LEFT, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"aaäåãâàá"},
                {1, u"sšsşś"},
                {1, u"ddđď"},
                {1, u"f"},
                {1, u"gġgģĝğ"},
                {1, u"h"},
                {1, u"j"},
                {1, u"k"},
                {1, u"lĺŀłļľl"},
                {2, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        }, // 12
        {
                {1.5, u"", ImGuiKey_LeftShift},
                {1, u"zzžż"},
                {1, u"x"},
                {1, u"cçcċčć"},
                {1, u"v"},
                {1, u"b"},
                {1, u"nņńnň"},
                {1, u"m"},
                {1, u","},
                {1, u"."},
                // {1, u"-"},
                {1.5, u"", ImGuiKey_RightShift},
        }, // 12
        {
                {2, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"", key_symbols_letter, "?123"},
                {1, u"", key_layout, ICON_FA_GLOBE},
                {5, u" "},
                {3, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        } // 12
};

// See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fr_FR/main.qml
virtual_keyboard::layout azerty = {
        {
                {0.5, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"aàâæ"},
                {1, u"z"},
                {1, u"eéèêë"},
                {1, u"r"},
                {1, u"t"},
                {1, u"yÿ"},
                {1, u"uùûü"},
                {1, u"iîï"},
                {1, u"oôœ"},
                {1, u"p"},
                {1.5, u"", ImGuiKey_Backspace, ICON_FA_DELETE_LEFT, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"q"},
                {1, u"s"},
                {1, u"d"},
                {1, u"f"},
                {1, u"g"},
                {1, u"h"},
                {1, u"j"},
                {1, u"k"},
                {1, u"l"},
                {1, u"m"},
                {1, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        }, // 12
        {
                {1.5, u"", ImGuiKey_LeftShift},
                {1, u"w"},
                {1, u"x"},
                {1, u"cç"},
                {1, u"v"},
                {1, u"b"},
                {1, u"n"},
                {1, u","},
                {1, u"."},
                {1, u"-"},
                {1.5, u"", ImGuiKey_RightShift},
        }, // 12
        {
                {2, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"", key_symbols_letter, "?123"},
                {1, u"", key_layout, ICON_FA_GLOBE},
                {5, u" "},
                {3, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        } // 12
};

virtual_keyboard::layout digits = {
        {
                {1, u"1"},
                {1, u"2"},
                {1, u"3"},
        }, // 3
        {
                {1, u"4"},
                {1, u"5"},
                {1, u"6"},
        }, // 3
        {
                {1, u"7"},
                {1, u"8"},
                {1, u"9"},
        }, // 3
        {
                {2, u"0"},
                {1, u"", ImGuiKey_Backspace, ICON_FA_DELETE_LEFT, virtual_keyboard::key_flag_repeat},
        } // 3
};

// // See https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fallback/main.qml
// virtual_keyboard::layout qwertz = {
// };

std::array layouts = {&qwerty, &azerty};

virtual_keyboard::layout symbols = {
        {
                {0.5, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"1"},
                {1, u"2"},
                {1, u"3"},
                {1, u"4"},
                {1, u"5"},
                {1, u"6"},
                {1, u"7"},
                {1, u"8"},
                {1, u"9"},
                {1, u"0"},
                {1.5, u"", ImGuiKey_Backspace, ICON_FA_DELETE_LEFT, virtual_keyboard::key_flag_repeat},
        }, // 12
        {
                {1, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"@"},
                {1, u"#"},
                {1, u"%"},
                {1, u"&"},
                {1, u"*"},
                {1, u"_"},
                {1, u"-"},
                {1, u"+"},
                {1, u"("},
                {1, u")"},
                {1, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        }, // 12
        {
                {1.5, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
                {1, u"", ImGuiKey_None, "1/2", virtual_keyboard::key_flag_hidden},
                {1, u"\""},
                {1, u"<"},
                {1, u">"},
                {1, u"'"},
                {1, u":"},
                {1, u"/"},
                {1, u"!"},
                {1, u"?"},
                {1.5, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        }, // 12
        {
                {1, u"", key_symbols_letter, "ABC"},
                {1, u"", key_layout, ICON_FA_GLOBE},
                {5, u" "},
                {3, u"", ImGuiKey_None, "", virtual_keyboard::key_flag_hidden},
        },
};

static std::string to_utf8(char32_t c)
{
	std::string s;

	if (c < 0x80)
	{
		s += (char)c;
	}
	else if (c < 0x800)
	{
		s += (char)(0xc0 | ((c >> 6) & 0x1f));
		s += (char)(0x80 | (c & 0x3f));
	}
	else if (c < 0x10000)
	{
		s += (char)(0xe0 | ((c >> 12) & 0x0f));
		s += (char)(0x80 | ((c >> 6) & 0x3f));
		s += (char)(0x80 | (c & 0x3f));
	}
	else
	{
		s += (char)(0xf0 | ((c >> 18) & 0x07));
		s += (char)(0x80 | ((c >> 12) & 0x3f));
		s += (char)(0x80 | ((c >> 6) & 0x3f));
		s += (char)(0x80 | (c & 0x3f));
	}

	return s;
}

// static std::string to_utf8(std::u16string_view str)
// {
// 	std::string s;
// 	for(char16_t c: str)
// 		s += to_utf8(c);
//
// 	return s;
// }
//
// static std::u16string str_to_upper(std::u16string_view text)
// {
// 	UErrorCode err = U_ZERO_ERROR;
//
// 	size_t size = u_strToUpper(nullptr, 0, text.data(), text.size(), "" /* locale*/, &err);
//
// 	if (U_FAILURE(err))
// 		throw std::runtime_error(std::string("u_strToUpper: ") + u_errorName(err));
//
// 	std::u16string upper;
// 	upper.resize(size);
//
// 	err = U_ZERO_ERROR;
// 	u_strToUpper(nullptr, 0, text.data(), text.size(), "" /* locale*/, &err);
//
// 	if (U_FAILURE(err))
// 		throw std::runtime_error(std::string("u_strToUpper: ") + u_errorName(err));
//
// 	return upper;
// }

// Mostly copied from ImGui::ButtonBehavior, massively simplified for the keyboard use case, don't take focus when clicked
bool virtual_keyboard::button_behavior(const ImRect & bb, ImGuiID id, bool * out_hovered, bool * out_held, ImGuiButtonFlags flags)
{
	ImGuiContext & g = *GImGui;

	if ((flags & ImGuiButtonFlags_PressedOnMask_) == 0)
	{
		if (g.IO.MouseSource == ImGuiMouseSource_VRHandTracking)
			flags |= ImGuiButtonFlags_PressedOnClick;
		else
			flags |= ImGuiButtonFlags_PressedOnClickRelease;
	}

	bool pressed = false;
	bool hovered = g.HoveredWindow == g.CurrentWindow and ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
	// ImGui::ItemHoverable;

	// Mouse handling
	if (hovered)
	{
		// Changed wrt original imgui: only 1 button is present
		bool mouse_button_clicked = ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);

		// Process initial action
		if (mouse_button_clicked && active_id != id)
		{
			active_id = id;
			held_duration = 0;

			if (flags & ImGuiButtonFlags_PressedOnClick)
				pressed = true;
		}
	}

	// Process while held
	bool held = false;
	if (active_id == id)
	{
		if (ImGui::IsMouseDown(0, id))
		{
			held = true;

			float prev_held_duration = held_duration;
			held_duration += g.IO.DeltaTime;
			if (flags & ImGuiButtonFlags_Repeat and held_duration > g.IO.KeyRepeatDelay)
			{
				int n1 = (prev_held_duration - g.IO.KeyRepeatDelay) / g.IO.KeyRepeatRate;
				int n2 = (held_duration - g.IO.KeyRepeatDelay) / g.IO.KeyRepeatRate;

				if (n2 != n1)
					pressed = true;
			}
		}
		else
		{
			if (hovered && (flags & ImGuiButtonFlags_PressedOnClickRelease))
				pressed = true;

			active_id = 0;
		}
	}

	if (out_hovered)
		*out_hovered = hovered;

	if (out_held)
		*out_held = held;

	return pressed;
}

bool virtual_keyboard::draw_single_key(const key & k, int key_id, ImVec2 size_arg, bool & hovered)
{
	std::string key_label;

	bool is_shift = k.key == ImGuiKey_LeftShift or k.key == ImGuiKey_RightShift;

	if (k.characters.empty())
	{
		if (is_shift)
			key_label = (current_case_mode == case_mode::lower) ? ICON_FA_CHEVRON_UP : ICON_FA_CIRCLE_CHEVRON_UP;
		else
			key_label = k.glyph;
	}
	else if (current_case_mode == case_mode::lower)
	{
		key_label = to_utf8(k.characters[0]);
	}
	else
	{
		// FIXME: use ICU
		key_label = to_utf8(/*u_*/ towupper(k.characters[0]));
	}

	std::string key_label_id;
	if (k.characters.empty())
		key_label_id = "virtual_keyboard_key_" + std::to_string(k.key);
	else
		key_label_id = "virtual_keyboard_" + to_utf8(k.characters[0]);

	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(key_label_id.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(key_label.c_str(), key_label.c_str() + key_label.size(), false /* Don't hide text after # */);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	ImGuiButtonFlags flags = ImGuiButtonFlags_None;

	if (k.flag & key_flag_repeat)
		flags |= ImGuiButtonFlags_Repeat;

	bool held;
	bool pressed = button_behavior(bb, id, &hovered, &held, flags);

	// Render
	bool active = (held and hovered) or (is_shift and current_case_mode == case_mode::caps_lock);

	const ImU32 col = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered
	                                                                              : ImGuiCol_Button);
	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

	// ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, key_label.c_str(), nullptr, &label_size, style.ButtonTextAlign, &bb);
	ImGui::RenderTextClippedEx(
	        g.CurrentWindow->DrawList,
	        bb.Min + style.FramePadding,
	        bb.Max - style.FramePadding,
	        key_label.c_str(),
	        key_label.c_str() + key_label.size(),
	        &label_size,
	        style.ButtonTextAlign,
	        &bb);

	if (pressed)
	{
		press_single_key(k);
	}

	return pressed;
}

void virtual_keyboard::press_single_key(const key & k)
{
	if (k.key == ImGuiKey_LeftShift or k.key == ImGuiKey_RightShift)
	{
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
	}
	else if (k.key != ImGuiKey_None)
	{
		ImGui::GetIO().AddKeyEvent(k.key, true);
		ImGui::GetIO().AddKeyEvent(k.key, false);
	}
	else if (not k.characters.empty())
	{
		switch (current_case_mode)
		{
			case case_mode::lower:
				ImGui::GetIO().AddInputCharactersUTF8(to_utf8(k.characters[0]).c_str());
				break;

			case case_mode::upper:
				current_case_mode = case_mode::lower;
				[[fallthrough]];

			case case_mode::caps_lock:
				ImGui::GetIO().AddInputCharactersUTF8(to_utf8(towupper(k.characters[0])).c_str());
				break;
		}
	}
}

void virtual_keyboard::display(ImGuiID & hovered_id)
{
	ImGuiStyle & style = ImGui::GetStyle();

	ImGuiInputTextState * input_state = ImGui::GetInputTextState(ImGui::GetCurrentContext()->ActiveId);
	bool want_digits = input_state and (input_state->Flags & ImGuiInputTextFlags_CharsDecimal) != 0;

	auto & layout = want_digits ? digits : symbols_shown ? symbols
	                                                     : *layouts[current_layout];

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

	// Compute keys size;
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

				bool is_hovered;
				if (draw_single_key(key, id++, ImVec2{base_key_width * key.width - style.ItemSpacing.x, key_height}, is_hovered))
				{
					switch ((int)key.key) // cast to int to avoid -Werror=switch
					{
						case key_layout:
							current_layout = (current_layout + 1) % layouts.size();
							break;

						case key_symbols_letter:
							symbols_shown = not symbols_shown;
							break;

						default:
							break;
					}
				}

				if (is_hovered)
					hovered_id = ImGui::GetItemID();
			}

			key_position.x += base_key_width * key.width;
		}

		row_position.y += key_height + style.ItemSpacing.y;
	}

	row_position.y -= style.ItemSpacing.y;

	ImGui::SetCursorPos(row_position);

	if (ImGui::IsWindowHovered())
	{
		ImGui::GetIO().MouseDown[0] = false;
		ImGui::GetIO().MouseClicked[0] = false;
	}

	ImGui::End();
	ImGui::PopStyleColor(); // ImGuiCol_WindowBg
	ImGui::PopStyleVar(4);  // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_WindowRounding, ImGuiStyleVar_ItemSpacing, ImGuiStyleVar_FrameRounding
}
