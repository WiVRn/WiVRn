/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "imgui_utils.h"

#include "imgui_internal.h"
#include "utils/strings.h"
#include <cstdlib>
#include <utility>

// https://github.com/ocornut/imgui/issues/3379#issuecomment-2943903877
void ScrollWhenDragging()
{
	ImVec2 delta{0.0f, -ImGui::GetIO().MouseDelta.y};
	const ImGuiMouseButton mouse_button = ImGuiMouseButton_Left;

	ImGuiContext & g = *ImGui::GetCurrentContext();
	ImGuiWindow * window = g.CurrentWindow;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);

	static int active_id;
	static ImVec2 cumulated_delta;

	bool HoveredIdAllowOverlap_backup = std::exchange(g.HoveredIdAllowOverlap, true);
	bool ActiveIdAllowOverlap_backup = std::exchange(g.ActiveIdAllowOverlap, true);
	if (active_id == 0 and ImGui::ItemHoverable(window->Rect(), 0, g.CurrentItemFlags) and ImGui::IsMouseClicked(mouse_button, ImGuiInputFlags_None, /*id*/ ImGuiKeyOwner_Any))
	{
		active_id = id;

		// Don't scroll on the first step, in case the active controller just changed
		delta = {};
		cumulated_delta = {};
	}

	if (not g.IO.MouseDown[mouse_button])
		active_id = 0;

	if (active_id == id)
	{
		if (delta.x != 0.0f)
			ImGui::SetScrollX(window, window->Scroll.x + delta.x);
		if (delta.y != 0.0f)
			ImGui::SetScrollY(window, window->Scroll.y + delta.y);

		cumulated_delta += delta;
		if (std::max(std::abs(cumulated_delta.x), std::abs(cumulated_delta.y)) > 50)
			ImGui::ClearActiveID();
	}

	g.HoveredIdAllowOverlap = HoveredIdAllowOverlap_backup;
	g.ActiveIdAllowOverlap = ActiveIdAllowOverlap_backup;
}

void CenterTextH(const std::string & text)
{
	float win_width = ImGui::GetWindowSize().x;

	std::vector<std::string> lines = utils::split(text);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
	for (const auto & i: lines)
	{
		float text_width = ImGui::CalcTextSize(i.c_str()).x;
		ImGui::SetCursorPosX((win_width - text_width) / 2);
		ImGui::Text("%s", i.c_str());
	}
	ImGui::PopStyleVar();
	ImGui::Dummy({}); // Make sure the original vertical ItemSpacing is respected
}

void CenterTextHV(const std::string & text)
{
	ImVec2 size = ImGui::GetWindowSize();

	std::vector<std::string> lines = utils::split(text);

	float text_height = 0;
	for (const auto & i: lines)
		text_height += ImGui::CalcTextSize(i.c_str()).y;

	ImGui::SetCursorPosY((size.y - text_height) / 2);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
	for (const auto & i: lines)
	{
		float text_width = ImGui::CalcTextSize(i.c_str()).x;
		ImGui::SetCursorPosX((size.x - text_width) / 2);
		ImGui::Text("%s", i.c_str());
	}
	ImGui::PopStyleVar();
}

void InputText(const char * label, std::string & text, const ImVec2 & size, ImGuiInputTextFlags flags)
{
	auto callback = [](ImGuiInputTextCallbackData * data) -> int {
		std::string & text = *reinterpret_cast<std::string *>(data->UserData);

		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			assert(text.data() == data->Buf);
			text.resize(data->BufTextLen);
			data->Buf = text.data();
		}

		return 0;
	};

	ImGui::InputTextEx(label, nullptr, text.data(), text.size() + 1, size, flags | ImGuiInputTextFlags_CallbackResize, callback, &text);
}

bool RadioButtonWithoutCheckBox(const std::string & label, bool active, ImVec2 size_arg)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(label.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);

	const ImVec2 pos = window->DC.CursorPos;

	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	ImGuiCol_ col;
	if ((held && hovered) || active)
		col = ImGuiCol_ButtonActive;
	else if (hovered)
		col = ImGuiCol_ButtonHovered;
	else
		col = ImGuiCol_Button;

	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(col), true, style.FrameRounding);

	ImVec2 TextAlign{0, 0.5f};
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label.c_str(), NULL, &label_size, TextAlign, &bb);

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label.c_str(), g.LastItemData.StatusFlags);
	return pressed;
}
