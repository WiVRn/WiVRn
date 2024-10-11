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

#include "imgui.h"
#include "imgui_internal.h"
#include "lobby.h"

#include <cwchar>
#include <ranges>
#include <unicode/uchar.h> // u_toupper

struct key
{
	float width;
	const std::u32string_view characters;
	ImGuiKey key = ImGuiKey_None;
	bool visible = true;
};

using keyboard_layout = std::vector<std::vector<key>>;

keyboard_layout azerty = {
        {
                {1, U"aàâæ"},
                {1, U"z"},
                {1, U"eéèêë"},
                {1, U"r"},
                {1, U"t"},
                {1, U"yÿ"},
                {1, U"uùûü"},
                {1, U"iîï"},
                {1, U"oôœ"},
                {1, U"p"},
                {2, U"", ImGuiKey_Backslash},
        },
        {
                {1, U"q"},
                {1, U"s"},
                {1, U"d"},
                {1, U"f"},
                {1, U"g"},
                {1, U"h"},
                {1, U"j"},
                {1, U"k"},
                {1, U"l"},
                {1, U"m"},
        },
        {
                {2, U"", ImGuiKey_LeftShift},
                {1, U"w"},
                {1, U"x"},
                {1, U"cç"},
                {1, U"v"},
                {1, U"b"},
                {1, U"n"},
                {1, U",?"},
                {1, U";."},
                {1, U":/"},
                {1, U"!§"},
        }};

keyboard_layout symbols = {
        {
                {1, U"1"},
                {1, U"2"},
                {1, U"3"},
                {1, U"4"},
                {1, U"5"},
                {1, U"6"},
                {1, U"7"},
                {1, U"8"},
                {1, U"9"},
                {1, U"0"},
        },
        {
                {1, U"@"},
                {1, U"#"},
                {1, U"%"},
                {1, U"&"},
                {1, U"*"},
                {1, U"_"},
                {1, U"-"},
                {1, U"+"},
                {1, U"("},
                {1, U")"},
        },
        {
                {1, U"\""},
                {1, U"<"},
                {1, U">"},
                {1, U"'"},
                {1, U"*"},
                {1, U":"},
                {1, U"/"},
                {1, U"!"},
                {1, U"?"},
                {1, U""},
        }};

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

static bool button_behavior(const ImRect & bb, ImGuiID id, bool * out_hovered, bool * out_held, ImGuiButtonFlags flags)
{
	ImGuiContext & g = *GImGui;
	ImGuiWindow * window = ImGui::GetCurrentWindow();

	// Default behavior inherited from item flags
	// Note that _both_ ButtonFlags and ItemFlags are valid sources, so copy one into the item_flags and only check that.
	ImGuiItemFlags item_flags = (g.LastItemData.ID == id ? g.LastItemData.InFlags : g.CurrentItemFlags);
	if (flags & ImGuiButtonFlags_AllowOverlap)
		item_flags |= ImGuiItemFlags_AllowOverlap;
	if (flags & ImGuiButtonFlags_Repeat)
		item_flags |= ImGuiItemFlags_ButtonRepeat;

	bool pressed = false;
	bool hovered = ImGui::ItemHoverable(bb, id, item_flags);

	// Mouse handling
	const ImGuiID test_owner_id = (flags & ImGuiButtonFlags_NoTestKeyOwner) ? ImGuiKeyOwner_Any : id;
	if (hovered)
	{
		// Poll mouse buttons
		// - 'mouse_button_clicked' is generally carried into ActiveIdMouseButton when setting ActiveId.
		// - Technically we only need some values in one code path, but since this is gated by hovered test this is fine.
		int mouse_button_clicked = -1;
		int mouse_button_released = -1;
		for (int button = 0; button < 3; button++)
			if (flags & (ImGuiButtonFlags_MouseButtonLeft << button)) // Handle ImGuiButtonFlags_MouseButtonRight and ImGuiButtonFlags_MouseButtonMiddle here.
			{
				if (ImGui::IsMouseClicked(button, test_owner_id) && mouse_button_clicked == -1)
				{
					mouse_button_clicked = button;
				}
				if (ImGui::IsMouseReleased(button, test_owner_id) && mouse_button_released == -1)
				{
					mouse_button_released = button;
				}
			}

		// Process initial action
		if (!(flags & ImGuiButtonFlags_NoKeyModifiers) || (!g.IO.KeyCtrl && !g.IO.KeyShift && !g.IO.KeyAlt))
		{
			if (mouse_button_clicked != -1 && g.ActiveId != id)
			{
				if (!(flags & ImGuiButtonFlags_NoSetKeyOwner))
					ImGui::SetKeyOwner(ImGui::MouseButtonToKey(mouse_button_clicked), id);
				if (flags & (ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnClickReleaseAnywhere))
				{
					// ImGui::SetActiveID(id, window);
					g.ActiveIdMouseButton = mouse_button_clicked;
					if (!(flags & ImGuiButtonFlags_NoNavFocus))
						ImGui::SetFocusID(id, window);
					ImGui::FocusWindow(window);
				}
				if ((flags & ImGuiButtonFlags_PressedOnClick) || ((flags & ImGuiButtonFlags_PressedOnDoubleClick) && g.IO.MouseClickedCount[mouse_button_clicked] == 2))
				{
					pressed = true;
					// if (flags & ImGuiButtonFlags_NoHoldingActiveId)
					//     ImGui::ClearActiveID();
					// else
					//     ImGui::SetActiveID(id, window); // Hold on ID
					if (!(flags & ImGuiButtonFlags_NoNavFocus))
						ImGui::SetFocusID(id, window);
					g.ActiveIdMouseButton = mouse_button_clicked;
					ImGui::FocusWindow(window);
				}
			}
			if (flags & ImGuiButtonFlags_PressedOnRelease)
			{
				if (mouse_button_released != -1)
				{
					const bool has_repeated_at_least_once = (item_flags & ImGuiItemFlags_ButtonRepeat) && g.IO.MouseDownDurationPrev[mouse_button_released] >= g.IO.KeyRepeatDelay; // Repeat mode trumps on release behavior
					if (!has_repeated_at_least_once)
						pressed = true;
					if (!(flags & ImGuiButtonFlags_NoNavFocus))
						ImGui::SetFocusID(id, window);
					// ImGui::ClearActiveID();
				}
			}

			// 'Repeat' mode acts when held regardless of _PressedOn flags (see table above).
			// Relies on repeat logic of IsMouseClicked() but we may as well do it ourselves if we end up exposing finer RepeatDelay/RepeatRate settings.
			if (g.ActiveId == id && (item_flags & ImGuiItemFlags_ButtonRepeat))
				if (g.IO.MouseDownDuration[g.ActiveIdMouseButton] > 0.0f && ImGui::IsMouseClicked(g.ActiveIdMouseButton, test_owner_id, ImGuiInputFlags_Repeat))
					pressed = true;
		}

		if (pressed)
			g.NavDisableHighlight = true;
	}

	// Process while held
	bool held = false;
	if (g.ActiveId == id)
	{
		if (g.ActiveIdSource == ImGuiInputSource_Mouse)
		{
			if (g.ActiveIdIsJustActivated)
				g.ActiveIdClickOffset = g.IO.MousePos - bb.Min;

			const int mouse_button = g.ActiveIdMouseButton;
			if (mouse_button == -1)
			{
				// Fallback for the rare situation were g.ActiveId was set programmatically or from another widget (e.g. #6304).
				// ImGui::ClearActiveID();
			}
			else if (ImGui::IsMouseDown(mouse_button, test_owner_id))
			{
				held = true;
			}
			else
			{
				bool release_in = hovered && (flags & ImGuiButtonFlags_PressedOnClickRelease) != 0;
				bool release_anywhere = (flags & ImGuiButtonFlags_PressedOnClickReleaseAnywhere) != 0;
				if ((release_in || release_anywhere) && !g.DragDropActive)
				{
					// Report as pressed when releasing the mouse (this is the most common path)
					bool is_double_click_release = (flags & ImGuiButtonFlags_PressedOnDoubleClick) && g.IO.MouseReleased[mouse_button] && g.IO.MouseClickedLastCount[mouse_button] == 2;
					bool is_repeating_already = (item_flags & ImGuiItemFlags_ButtonRepeat) && g.IO.MouseDownDurationPrev[mouse_button] >= g.IO.KeyRepeatDelay; // Repeat mode trumps <on release>
					bool is_button_avail_or_owned = ImGui::TestKeyOwner(ImGui::MouseButtonToKey(mouse_button), test_owner_id);
					if (!is_double_click_release && !is_repeating_already && is_button_avail_or_owned)
						pressed = true;
				}
				// ImGui::ClearActiveID();
			}
			if (!(flags & ImGuiButtonFlags_NoNavFocus))
				g.NavDisableHighlight = true;
		}
		else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
		{
			// When activated using Nav, we hold on the ActiveID until activation button is released
			// if (g.NavActivateDownId != id)
			//     ImGui::ClearActiveID();
		}
		if (pressed)
			g.ActiveIdHasBeenPressedBefore = true;
	}

	if (out_hovered)
		*out_hovered = hovered;
	if (out_held)
		*out_held = held;

	return pressed;
}

static void draw_single_key(const key & k, int key_id, ImVec2 size_arg)
{
	std::string label = k.characters == U"" ? "" : to_utf8(k.characters[0]);
	label += "##virtual_keyboard_key_" + std::to_string(key_id);

	// ImGui::Button(label.c_str(), size);

	int flags = (int)ImGuiButtonFlags_PressedOnClickRelease | (int)ImGuiButtonFlags_MouseButtonLeft | (int)ImGuiButtonFlags_NoNavFocus;

	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return /*false*/;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(label.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);

	ImVec2 pos = window->DC.CursorPos;
	if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
		pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return /*false*/;

	bool hovered, held;
	[[maybe_unused]] bool pressed = button_behavior(bb, id, &hovered, &held, flags);

	// Render
	const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered
	                                                                                         : ImGuiCol_Button);
	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

	// if (g.LogEnabled)
	// LogSetNextTextDecoration("[", "]");
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label.c_str(), NULL, &label_size, style.ButtonTextAlign, &bb);

	// Automatically close popups
	// if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
	//    CloseCurrentPopup();

	// IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
	// return pressed;
}

void scenes::lobby::gui_keyboard(ImVec2 size)
{
	const keyboard_layout & layout = azerty;

	int nb_rows = layout.size();

	ImVec2 position = ImGui::GetCursorPos();
	ImGuiStyle & style = ImGui::GetStyle();

	std::vector<float> keys_width;

	// Size of a key including margin, scaled by the key width later
	ImVec2 key_size{FLT_MAX, (size.y + style.FramePadding.y) / nb_rows};
	for (const auto & row: layout)
	{
		float total_width = 0;
		for (auto & key: row)
			total_width += key.width;
		keys_width.push_back(total_width);

		float key_width = (size.x + style.FramePadding.x) / total_width;
		key_size.x = std::min(key_size.x, key_width);
	}

	int id = 0;
	for (const auto & [row, width]: std::views::zip(layout, keys_width))
	{
		ImVec2 key_position = position;

		for (const auto & key: row)
		{
			if (key.visible)
			{
				ImGui::SetCursorPos(key_position);

				draw_single_key(key, id++, ImVec2{key_size.x * key.width - style.FramePadding.x, key_size.y - style.FramePadding.y});
			}

			key_position.x += key_size.x * key.width;
		}

		position.y += key_size.y;
	}

	ImGui::SetCursorPos(position);
}
