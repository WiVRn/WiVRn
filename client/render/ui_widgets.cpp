/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "ui_widgets.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "ui_theme.h"

#include "IconsFontAwesome6.h"

#include <cstdio>
#include <functional>
#include <unordered_map>
#include <vector>

namespace wivrn::ui
{

namespace
{
std::function<void()> hover_haptic_hook;
std::function<void(const char *)> tooltip_hook;
} // namespace

void set_hover_haptic(std::function<void()> hook)
{
	hover_haptic_hook = std::move(hook);
}

void set_tooltip_hook(std::function<void(const char *)> hook)
{
	tooltip_hook = std::move(hook);
}

// Fire the hover haptic for the last submitted item, if a hook is installed.
// The hook itself checks whether that item is actually hovered.
static void hover_haptic()
{
	if (hover_haptic_hook)
		hover_haptic_hook();
}

// Show a tooltip for the last submitted item; caller ensures it is hovered.
static void show_tooltip(const char * text)
{
	if (text and text[0] and tooltip_hook)
		tooltip_hook(text);
}

void page_header(const char * title, const char * subtitle)
{
	const theme & t = current();

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * metrics::font_title);
	ImGui::TextUnformatted(title);
	ImGui::PopFont();

	if (subtitle and subtitle[0])
	{
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
		ImGui::TextUnformatted(subtitle);
		ImGui::PopStyleColor();
	}
	ImGui::Dummy({0, ImGui::GetStyle().ItemSpacing.y});
}

// A card is purely a visual rounded panel, NOT a child window: its content is
// laid out directly in the parent window so the page stays a single scroll
// surface (drag-to-scroll and clicks behave uniformly everywhere). The panel is
// drawn behind the upcoming content using the height measured last frame, then
// the full extent is reserved so layout and scrolling account for it.
// Always pair begin_card with end_card.
namespace
{
struct card_frame
{
	ImGuiID id;
	ImVec2 origin;
	float width;
	float content_max_x_backup;
	float work_max_x_backup;
};
std::vector<card_frame> card_stack;
std::unordered_map<ImGuiID, float> card_heights;
} // namespace

bool begin_card(const char * id, const ImVec2 &)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const theme & t = current();
	const ImGuiID gid = window->GetID(id);

	card_stack.push_back({gid,
	                      window->DC.CursorPos,
	                      ImGui::GetContentRegionAvail().x,
	                      window->ContentRegionRect.Max.x,
	                      window->WorkRect.Max.x});
	const card_frame & cf = card_stack.back();

	if (const float h = card_heights[gid]; h > 0)
	{
		const ImVec2 bb_max = {cf.origin.x + cf.width, cf.origin.y + h};
		window->DrawList->AddRectFilled(cf.origin, bb_max, t.col(t.card), t.card_rounding);
		if (t.border_size > 0)
			window->DrawList->AddRect(cf.origin, bb_max, t.col(t.border), t.card_rounding, 0, t.border_size);
	}

	ImGui::PushID(id);
	window->ContentRegionRect.Max.x -= metrics::card_padding.x;
	window->WorkRect.Max.x -= metrics::card_padding.x;
	ImGui::Indent(metrics::card_padding.x);
	ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, cf.origin.y + metrics::card_padding.y});
	ImGui::BeginGroup();
	return true;
}

void end_card()
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const card_frame cf = card_stack.back();
	card_stack.pop_back();

	ImGui::EndGroup();
	ImGui::Unindent(metrics::card_padding.x);
	window->ContentRegionRect.Max.x = cf.content_max_x_backup;
	window->WorkRect.Max.x = cf.work_max_x_backup;
	ImGui::PopID();

	const float height = ImGui::GetItemRectMax().y + metrics::card_padding.y - cf.origin.y;
	card_heights[cf.id] = height;

	// reserve the full card extent so layout and scrolling account for it
	ImGui::SetCursorScreenPos(cf.origin);
	ImGui::Dummy({cf.width, height});
}

void row_separator()
{
	const theme & t = current();
	ImGui::PushStyleColor(ImGuiCol_Separator, t.border);
	ImGui::Separator();
	ImGui::PopStyleColor();
}

void setting_label(const char * title, const char * description, float control_width)
{
	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	const ImVec2 start = ImGui::GetCursorPos();
	const float avail = ImGui::GetContentRegionAvail().x;
	float text_w = avail - control_width - style.ItemSpacing.x;
	if (text_w < 1)
		text_w = avail;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {style.ItemSpacing.x, metrics::label_line_gap});
	ImGui::PushTextWrapPos(start.x + text_w);
	ImGui::TextUnformatted(title);
	if (description and description[0])
	{
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * metrics::font_description);
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
		ImGui::TextUnformatted(description);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	ImGui::PopTextWrapPos();
	ImGui::PopStyleVar();

	// right-aligned control slot, vertically centered against the label block.
	// Controls are control_height x frame height, so centre against that.
	const float label_h = ImGui::GetCursorPos().y - start.y;
	const float ctrl_h = ImGui::GetFrameHeight() * metrics::control_height;
	const float ctrl_y = start.y + ImMax(0.f, (label_h - ctrl_h) * 0.5f);
	ImGui::SetCursorPos({start.x + avail - control_width, ctrl_y});
}

bool button(const char * label, button_style s, const ImVec2 & size)
{
	const theme & t = current();

	// default to the shared control height; width still auto-fits the label
	const float h = size.y > 0 ? size.y : ImGui::GetFrameHeight() * metrics::control_height;

	switch (s)
	{
		case button_style::primary:
			ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
			ImGui::PushStyleColor(ImGuiCol_Text, t.on_accent);
			break;
		case button_style::secondary:
			ImGui::PushStyleColor(ImGuiCol_Button, t.control);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.control_hovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.control_active);
			ImGui::PushStyleColor(ImGuiCol_Text, t.text);
			break;
		case button_style::danger:
			ImGui::PushStyleColor(ImGuiCol_Button, t.danger);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.danger_hovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.danger);
			ImGui::PushStyleColor(ImGuiCol_Text, t.on_accent);
			break;
		case button_style::ghost:
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.control);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.control_active);
			ImGui::PushStyleColor(ImGuiCol_Text, t.text);
			break;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, t.rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, metrics::button_padding);
	const bool pressed = ImGui::Button(label, {size.x, h});
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);
	hover_haptic();
	return pressed;
}

bool icon_button(const char * icon, const ImVec2 & size_arg, bool active, const char * tooltip)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const theme & t = current();

	// square touch target, same height as the sliders and other controls
	const float side = ImGui::GetFrameHeight() * metrics::control_height;
	const ImVec2 size = size_arg.x > 0 ? size_arg : ImVec2{side, side};

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	const ImGuiID gid = window->GetID(icon);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();
	if (hovered)
		show_tooltip(tooltip);

	ImVec4 bg = active ? t.accent : t.control;
	if (held)
		bg = active ? t.accent_active : t.control_active;
	else if (hovered)
		bg = active ? t.accent_hovered : t.control_hovered;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), t.rounding);

	// glyph drawn manually so it scales with the button. Centre on the glyph's real
	// ink box (X0..X1, Y0..Y1) rather than CalcTextSize, which returns the advance
	// width and line height and leaves the glyph up-and-left of true centre.
	const float glyph_size = size.y * metrics::icon_button_glyph;
	ImGui::PushFont(nullptr, glyph_size);
	const ImU32 col = t.col(active ? t.on_accent : t.text);
	const ImVec2 center = bb.GetCenter();

	unsigned int codepoint = 0;
	ImTextCharFromUtf8(&codepoint, icon, nullptr);
	const ImFontGlyph * glyph = ImGui::GetFontBaked()->FindGlyph((ImWchar)codepoint);
	if (glyph)
		draw->AddText({center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f}, col, icon);
	else
	{
		const ImVec2 ts = ImGui::CalcTextSize(icon);
		draw->AddText({center.x - ts.x * 0.5f, center.y - ts.y * 0.5f}, col, icon);
	}
	ImGui::PopFont();

	return pressed;
}

// Horizontal space a trailing reset slot consumes, gap included
static float reset_slot_width()
{
	return ImGui::GetFrameHeight() * metrics::control_height + ImGui::GetStyle().ItemSpacing.x;
}

// Trailing reset affordance placed on the same row as a value control, right after
// it (the control should have shrunk its width by reset_slot_width() to make room).
// Draws a reset icon button when show is true, or an empty slot when false so the
// right edge stays put. Returns true when the user taps reset.
static bool reset_slot(const char * id, bool show)
{
	const float side = ImGui::GetFrameHeight() * metrics::control_height;
	ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
	if (not show)
	{
		ImGui::Dummy({side, side});
		return false;
	}
	ImGui::PushID(id);
	const bool clicked = icon_button(ICON_FA_ARROW_ROTATE_LEFT, {side, side}, false, "Reset to default");
	ImGui::PopID();
	return clicked;
}

bool toggle(const char * id, bool * v, const bool * default_value)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	// match the height of the other controls (sliders, combo, ...)
	const float height = ImGui::GetFrameHeight() * metrics::control_height;
	const float width = height * metrics::toggle_aspect;
	const float radius = height * 0.5f;

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + ImVec2(width, height));
	ImGui::ItemSize(bb, style.FramePadding.y);
	const ImGuiID gid = window->GetID(id);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	bool changed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();
	if (changed)
		*v = not *v;

	ImVec4 track = *v ? t.accent : t.control;
	if (hovered)
		track = *v ? t.accent_hovered : t.control_hovered;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(track), radius);
	const float cx = *v ? (bb.Max.x - radius) : (bb.Min.x + radius);
	draw->AddCircleFilled({cx, bb.Min.y + radius}, radius - metrics::toggle_knob_inset, t.col(t.on_accent));

	if (default_value and reset_slot(id, *v != *default_value))
	{
		*v = *default_value;
		changed = true;
	}

	return changed;
}

bool slider_int(const char * id, int * v, int v_min, int v_max, const char * format, const ImVec2 & size_arg, const int * default_value)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const theme & t = current();
	const ImGuiID gid = window->GetID(id);

	ImVec2 size = ImGui::CalcItemSize(size_arg, ImGui::CalcItemWidth(), ImGui::GetFrameHeight() * metrics::control_height);
	if (default_value)
		size.x -= reset_slot_width();
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (not ImGui::ItemAdd(bb, gid, &bb))
		return false;

	// activation, mirrors ImGui::SliderScalar
	const bool hovered = ImGui::ItemHoverable(bb, gid, g.LastItemData.ItemFlags);
	hover_haptic();
	const bool clicked = hovered and ImGui::IsMouseClicked(ImGuiMouseButton_Left, ImGuiInputFlags_None, gid);
	if (clicked or g.NavActivateId == gid)
	{
		if (clicked)
			ImGui::SetKeyOwner(ImGuiKey_MouseLeft, gid);
		ImGui::SetActiveID(gid, window);
		ImGui::SetFocusID(gid, window);
		ImGui::FocusWindow(window);
		g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
	}

	ImRect grab;
	const bool changed = ImGui::SliderBehavior(bb, gid, ImGuiDataType_S32, v, &v_min, &v_max, format, ImGuiSliderFlags_None, &grab);
	if (changed)
		ImGui::MarkItemEdited(gid);

	const float fraction = v_max > v_min ? float(*v - v_min) / float(v_max - v_min) : 0;
	const float fill_x = bb.Min.x + (bb.Max.x - bb.Min.x) * fraction;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(t.control), t.rounding);
	if (fraction > 0)
		draw->AddRectFilled(bb.Min, {fill_x, bb.Max.y}, t.col(t.accent_active), t.rounding, ImDrawFlags_RoundCornersLeft);
	// bright grab at the fill edge
	const float grab_w = metrics::slider_grab_width;
	const float gx = ImClamp(fill_x, bb.Min.x + grab_w * 0.5f, bb.Max.x - grab_w * 0.5f);
	draw->AddRectFilled({gx - grab_w * 0.5f, bb.Min.y}, {gx + grab_w * 0.5f, bb.Max.y}, t.col(t.accent), t.rounding);

	char buf[64];
	std::snprintf(buf, sizeof(buf), format, *v);
	const ImVec2 ts = ImGui::CalcTextSize(buf);
	draw->AddText({bb.Min.x + (size.x - ts.x) * 0.5f, bb.Min.y + (size.y - ts.y) * 0.5f}, t.col(t.text), buf);

	bool reset = false;
	if (default_value and reset_slot(id, *v != *default_value))
	{
		*v = *default_value;
		reset = true;
	}

	return changed or reset;
}

bool segmented(const char * id, const std::vector<std::string> & options, int * selected, const ImVec2 & size_arg, const int * default_value)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems or options.empty())
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const theme & t = current();

	ImVec2 size = ImGui::CalcItemSize(size_arg, ImGui::CalcItemWidth(), ImGui::GetFrameHeight() * metrics::control_height);
	if (default_value)
		size.x -= reset_slot_width();
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	const ImGuiID gid = window->GetID(id);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	const bool hovered = ImGui::ItemHoverable(bb, gid, g.LastItemData.ItemFlags);
	hover_haptic();
	// claim the active id on press so a drag here scrolls nothing (see ScrollWhenDragging)
	if (hovered and ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		ImGui::SetActiveID(gid, window);
		ImGui::FocusWindow(window);
	}
	if (g.ActiveId == gid and not g.IO.MouseDown[ImGuiMouseButton_Left])
		ImGui::ClearActiveID();

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(t.control), t.rounding);

	const int n = int(options.size());
	const float seg_w = size.x / n;
	const float pad = metrics::segmented_inset;
	bool changed = false;

	for (int i = 0; i < n; ++i)
	{
		const ImRect seg({bb.Min.x + seg_w * i, bb.Min.y}, {bb.Min.x + seg_w * (i + 1), bb.Max.y});
		const bool seg_hovered = hovered and seg.Contains(g.IO.MousePos);
		if (seg_hovered and ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			*selected = i;
			changed = true;
		}

		const bool active = *selected == i;
		if (active)
			draw->AddRectFilled(seg.Min + ImVec2(pad, pad), seg.Max - ImVec2(pad, pad), t.col(t.accent), t.rounding);
		else if (seg_hovered)
			draw->AddRectFilled(seg.Min + ImVec2(pad, pad), seg.Max - ImVec2(pad, pad), t.col(t.control_hovered), t.rounding);

		const char * label = options[i].c_str();
		const ImVec2 ts = ImGui::CalcTextSize(label);
		draw->AddText({seg.Min.x + (seg_w - ts.x) * 0.5f, seg.Min.y + (size.y - ts.y) * 0.5f}, t.col(active ? t.on_accent : t.text), label);
	}

	if (default_value and reset_slot(id, *selected != *default_value))
	{
		*selected = *default_value;
		changed = true;
	}

	return changed;
}

namespace
{
// Where combo modals open, in imgui display coords. Set each frame by the lobby.
ImVec2 popup_center = {0, 0};

// One selectable option inside the combo modal. A tall, full-width touch target
// with the name (accent when selected) and an optional muted description.
bool combo_row(const combo_item & item, bool selected)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const theme & t = current();

	const float row_h = ImGui::GetFrameHeight() * metrics::combo_row_height;
	const float row_w = ImGui::GetContentRegionAvail().x;
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + ImVec2(row_w, row_h));
	ImGui::ItemSize(bb, 0);
	const ImGuiID gid = window->GetID(item.name);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();

	ImVec4 bg = selected ? t.control_active : t.card;
	if (held)
		bg = t.control_active;
	else if (hovered)
		bg = t.control_hovered;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), t.rounding);
	if (selected)
		draw->AddRectFilled(bb.Min, {bb.Min.x + metrics::combo_padding.x * 0.25f, bb.Max.y}, t.col(t.accent), t.rounding, ImDrawFlags_RoundCornersLeft);

	const float tx = bb.Min.x + metrics::combo_padding.x;
	const ImU32 name_col = t.col(selected ? t.accent : t.text);
	const ImVec2 nts = ImGui::CalcTextSize(item.name);

	if (item.description and item.description[0])
	{
		const float desc_size = ImGui::GetStyle().FontSizeBase * metrics::font_description;
		ImGui::PushFont(nullptr, desc_size);
		const ImVec2 dts = ImGui::CalcTextSize(item.description);
		const float total = nts.y + metrics::label_line_gap + dts.y;
		const float y = bb.Min.y + (row_h - total) * 0.5f;
		draw->AddText({tx, y + nts.y + metrics::label_line_gap}, t.col(t.text_muted), item.description);
		ImGui::PopFont();
		draw->AddText({tx, y}, name_col, item.name);
	}
	else
	{
		draw->AddText({tx, bb.Min.y + (row_h - nts.y) * 0.5f}, name_col, item.name);
	}

	return pressed;
}
} // namespace

void set_popup_center(const ImVec2 & center)
{
	popup_center = center;
}

bool combo(const char * id, const char * title, const std::vector<combo_item> & items, int * selected, float width, const int * default_value)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems or items.empty())
		return false;

	const ImGuiStyle & style = ImGui::GetStyle();
	const theme & t = current();
	const ImGuiID gid = window->GetID(id);

	// closed box, same height as the slider so setting rows line up
	ImVec2 size = ImGui::CalcItemSize({width, 0}, ImGui::CalcItemWidth(), ImGui::GetFrameHeight() * metrics::control_height);
	if (default_value)
		size.x -= reset_slot_width();
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool clicked = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();

	ImVec4 box = t.control;
	if (held)
		box = t.control_active;
	else if (hovered)
		box = t.control_hovered;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(box), t.rounding);
	if (t.border_size > 0)
		draw->AddRect(bb.Min, bb.Max, t.col(t.border), t.rounding, 0, t.border_size);

	const int sel = ImClamp(*selected, 0, int(items.size()) - 1);

	// preview, left aligned and clipped to the box
	const char * preview = items[sel].name;
	const ImVec2 ts = ImGui::CalcTextSize(preview);
	const float pad_x = metrics::combo_padding.x;
	draw->PushClipRect(bb.Min, bb.Max, true);
	draw->AddText({bb.Min.x + pad_x, bb.Min.y + (size.y - ts.y) * 0.5f}, t.col(t.text), preview);
	draw->PopClipRect();

	// chevron on the right
	const float cx = bb.Max.x - pad_x - metrics::combo_chevron * 0.5f;
	const float cy = bb.Min.y + size.y * 0.5f;
	const float r = metrics::combo_chevron * 0.5f;
	draw->AddTriangleFilled({cx - r, cy - r * 0.4f}, {cx + r, cy - r * 0.4f}, {cx, cy + r * 0.6f}, t.col(t.text_muted));

	char popup_id[64];
	std::snprintf(popup_id, sizeof(popup_id), "##combo_%08X", gid);
	if (clicked)
		ImGui::OpenPopup(popup_id);

	ImGui::SetNextWindowPos(popup_center, ImGuiCond_Always, {0.5f, 0.5f});
	ImGui::SetNextWindowSize({ImMax(size.x, metrics::combo_modal_min_width), 0});

	ImGui::PushStyleColor(ImGuiCol_PopupBg, t.card);
	ImGui::PushStyleColor(ImGuiCol_Border, t.border);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, t.card_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, metrics::card_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, t.border_size > 0 ? t.border_size : 1);

	bool changed = false;
	if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (title and title[0])
		{
			ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::Dummy({0, style.ItemSpacing.y});
		}

		for (int i = 0; i < int(items.size()); ++i)
		{
			if (combo_row(items[i], i == sel))
			{
				*selected = i;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}

		// tapping outside the list dismisses without changing the selection.
		// AllowWhenBlockedByActiveItem is required: pressing a row sets it as the active
		// item, which would otherwise make IsWindowHovered return false on the press
		// frame and close the modal before the row's release registers the selection.
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) and not ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(3);

	if (default_value and reset_slot(id, *selected != *default_value))
	{
		*selected = *default_value;
		changed = true;
	}

	return changed;
}

void chip(const char * label, chip_style style, bool dot)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	const theme & t = current();

	// foreground (text/dot/icon) and the pill fill
	ImVec4 fg = t.text;
	ImVec4 bg = t.control;
	switch (style)
	{
		case chip_style::neutral:
			break;
		case chip_style::muted:
			fg = t.text_muted;
			break;
		case chip_style::accent:
			fg = t.accent;
			bg = {t.accent.x, t.accent.y, t.accent.z, 0.16f};
			break;
		case chip_style::success:
			fg = t.success;
			bg = {t.success.x, t.success.y, t.success.z, 0.16f};
			break;
		case chip_style::warning:
			fg = t.warning;
			bg = {t.warning.x, t.warning.y, t.warning.z, 0.16f};
			break;
		case chip_style::danger:
			fg = t.danger;
			bg = {t.danger.x, t.danger.y, t.danger.z, 0.16f};
			break;
	}

	const ImVec2 ts = ImGui::CalcTextSize(label);
	const ImVec2 pad = metrics::chip_padding;
	const float dot_r = ts.y * 0.26f;
	const float dot_gap = dot ? dot_r * 2 + pad.x * 0.55f : 0;
	const ImVec2 size = {ts.x + pad.x * 2 + dot_gap, ts.y + pad.y * 2};

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb);
	if (not ImGui::ItemAdd(bb, 0))
		return;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), size.y * 0.5f);

	float tx = bb.Min.x + pad.x;
	if (dot)
	{
		draw->AddCircleFilled({tx + dot_r, bb.Min.y + size.y * 0.5f}, dot_r, t.col(fg));
		tx += dot_r * 2 + pad.x * 0.55f;
	}
	draw->AddText({tx, bb.Min.y + pad.y}, t.col(fg), label);
}

void nav_section(const char * label)
{
	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	ImGui::Dummy({0, style.ItemSpacing.y * 0.5f});
	ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_description);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::Dummy({0, 2});
}

bool nav_item(const char * icon, const char * label, bool selected)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	const float h = ImGui::GetFrameHeight() * metrics::control_height;
	const float w = ImGui::GetContentRegionAvail().x;
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + ImVec2(w, h));
	ImGui::ItemSize(bb, style.FramePadding.y);
	const ImGuiID gid = window->GetID(label);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();

	ImDrawList * draw = window->DrawList;
	const ImVec4 bg = selected ? t.control : (hovered ? t.control_hovered : ImVec4{0, 0, 0, 0});
	if (bg.w > 0)
		draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), t.rounding);

	const float pad = metrics::combo_padding.x;
	const float icon_w = ImGui::GetFontSize() * 1.5f; // icon column

	const ImVec2 its = ImGui::CalcTextSize(icon);
	draw->AddText({bb.Min.x + pad + (icon_w - its.x) * 0.5f, bb.Min.y + (h - its.y) * 0.5f}, t.col(selected ? t.accent : t.text_muted), icon);

	const ImVec2 ls = ImGui::CalcTextSize(label);
	draw->AddText({bb.Min.x + pad + icon_w + pad * 0.5f, bb.Min.y + (h - ls.y) * 0.5f}, t.col(t.text), label);

	return pressed;
}

static int input_text_resize(ImGuiInputTextCallbackData * data)
{
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
	{
		std::string & s = *static_cast<std::string *>(data->UserData);
		s.resize(data->BufTextLen);
		data->Buf = s.data();
	}
	return 0;
}

// Common frame styling for the input widgets, control height. Returns the count of
// style colors / vars pushed so the caller can pop them.
static void push_input_style(float height)
{
	const theme & t = current();
	ImGui::PushStyleColor(ImGuiCol_FrameBg, t.control);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, t.control_hovered);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, t.control_active);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text);
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.text_muted);
	ImGui::PushStyleColor(ImGuiCol_Border, t.border);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, t.rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, t.border_size);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {metrics::input_padding_x, (height - ImGui::GetFontSize()) * 0.5f});
}

static void pop_input_style()
{
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(6);
}

bool input_text(const char * id, std::string & text, const char * hint, float width)
{
	push_input_style(ImGui::GetFrameHeight() * metrics::control_height);
	const bool changed = ImGui::InputTextEx(id, hint, text.data(), int(text.size()) + 1, {width, 0}, ImGuiInputTextFlags_CallbackResize, input_text_resize, &text);
	pop_input_style();
	hover_haptic();
	return changed;
}

bool input_int(const char * id, int * v, int step, int v_min, int v_max, float width)
{
	const ImGuiStyle & style = ImGui::GetStyle();
	const float side = ImGui::GetFrameHeight() * metrics::control_height;
	const float gap = style.ItemSpacing.x;
	const float total = width > 0 ? width : ImGui::CalcItemWidth();
	const float field_w = total - 2 * (side + gap);

	ImGui::PushID(id);

	push_input_style(side);
	ImGui::SetNextItemWidth(field_w);
	bool changed = ImGui::InputInt("##field", v, 0, 0, ImGuiInputTextFlags_CharsDecimal); // step 0 hides native buttons
	pop_input_style();
	hover_haptic();

	ImGui::SameLine(0, gap);
	if (icon_button(ICON_FA_MINUS, {side, side}))
	{
		*v -= step;
		changed = true;
	}
	ImGui::SameLine(0, gap);
	if (icon_button(ICON_FA_PLUS, {side, side}))
	{
		*v += step;
		changed = true;
	}

	if (v_min != v_max)
		*v = ImClamp(*v, v_min, v_max);

	ImGui::PopID();
	return changed;
}

bool begin_modal(const char * id, const char * title, float width)
{
	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	ImGui::SetNextWindowPos(popup_center, ImGuiCond_Always, {0.5f, 0.5f});
	if (width > 0)
		ImGui::SetNextWindowSize({width, 0});

	ImGui::PushStyleColor(ImGuiCol_PopupBg, t.card);
	ImGui::PushStyleColor(ImGuiCol_Border, t.border);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, t.card_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, metrics::card_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, t.border_size > 0 ? t.border_size : 1);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	if (width <= 0)
		flags |= ImGuiWindowFlags_AlwaysAutoResize;

	const bool open = ImGui::BeginPopupModal(id, nullptr, flags);
	if (open)
	{
		if (title and title[0])
		{
			ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_modal_title);
			ImGui::TextUnformatted(title);
			ImGui::PopFont();
			ImGui::Dummy({0, style.ItemSpacing.y});
		}
	}
	else
	{
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(3);
	}
	return open;
}

void end_modal()
{
	ImGui::EndPopup();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(3);
}

} // namespace wivrn::ui
