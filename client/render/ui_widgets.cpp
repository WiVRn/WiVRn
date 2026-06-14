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

#include <algorithm>
#include <cmath>
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

// Show a tooltip for the last item; caller ensures it is hovered
static void show_tooltip(const std::string & text)
{
	if (not text.empty() and tooltip_hook)
		tooltip_hook(text.c_str());
}

void page_header(const std::string & title, const std::string & subtitle)
{
	const theme & t = current();

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * metrics::font_title);
	ImGui::TextUnformatted(title.c_str());
	ImGui::PopFont();

	if (not subtitle.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
		ImGui::TextUnformatted(subtitle.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Dummy({0, ImGui::GetStyle().ItemSpacing.y});
}

// A card is a visual rounded panel, not a child window, so the page stays one scroll
// surface. Drawn behind the content using last frame's height; pair with end_card.
namespace
{
struct card_frame
{
	ImGuiID id;
	ImVec2 origin;
	float width;
	float content_max_x_backup;
	float work_max_x_backup;
	ImVec2 padding;
};
std::vector<card_frame> card_stack;
std::unordered_map<ImGuiID, float> card_heights;

bool begin_card_impl(const char * id, ImVec2 padding)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const theme & t = current();
	const ImGuiID gid = window->GetID(id);

	card_stack.push_back({gid,
	                      window->DC.CursorPos,
	                      ImGui::GetContentRegionAvail().x,
	                      window->ContentRegionRect.Max.x,
	                      window->WorkRect.Max.x,
	                      padding});
	const card_frame & cf = card_stack.back();

	if (const float h = card_heights[gid]; h > 0)
	{
		const ImVec2 bb_max = {cf.origin.x + cf.width, cf.origin.y + h};
		ImVec4 card = t.card;
		card.w *= background_alpha(); // card fill follows the panel opacity
		window->DrawList->AddRectFilled(cf.origin, bb_max, t.col(card), t.card_rounding);
		if (t.border_size > 0)
			window->DrawList->AddRect(cf.origin, bb_max, t.col(t.border), t.card_rounding, 0, t.border_size);
	}

	ImGui::PushID(id);
	window->ContentRegionRect.Max.x -= padding.x;
	window->WorkRect.Max.x -= padding.x;
	ImGui::Indent(padding.x);
	ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, cf.origin.y + padding.y});
	ImGui::BeginGroup();
	return true;
}
} // namespace

bool begin_card(const char * id, const ImVec2 &)
{
	return begin_card_impl(id, metrics::card_padding);
}

bool begin_list_card(const char * id)
{
	return begin_card_impl(id, metrics::list_card_padding);
}

void end_card()
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const card_frame cf = card_stack.back();
	card_stack.pop_back();

	ImGui::EndGroup();
	ImGui::Unindent(cf.padding.x);
	window->ContentRegionRect.Max.x = cf.content_max_x_backup;
	window->WorkRect.Max.x = cf.work_max_x_backup;
	ImGui::PopID();

	const float height = ImGui::GetItemRectMax().y + cf.padding.y - cf.origin.y;
	card_heights[cf.id] = height;

	// reserve the full card extent so layout and scrolling account for it
	ImGui::SetCursorScreenPos(cf.origin);
	ImGui::Dummy({cf.width, height});
}

void row_separator()
{
	const theme & t = current();
	ImVec4 edge = t.border;
	edge.w *= background_alpha(); // match the panel/card opacity
	ImGui::PushStyleColor(ImGuiCol_Separator, edge);
	ImGui::Separator();
	ImGui::PopStyleColor();
}

float setting_label(const std::string & title, const std::string & description, float control_width)
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
	ImGui::TextUnformatted(title.c_str());
	ImGui::PopTextWrapPos();
	if (not description.empty())
	{
		ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_description);
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
		// never wider than half the row; wrap to as many lines as needed so it shows in full
		const float desc_w = ImMin(text_w, avail * 0.5f);
		ImGui::PushTextWrapPos(start.x + desc_w);
		ImGui::TextUnformatted(description.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	ImGui::PopStyleVar();

	// right-aligned control slot, vertically centered against the label block
	const float label_h = ImGui::GetCursorPos().y - start.y;
	const float ctrl_h = ImGui::GetFrameHeight() * metrics::control_height;
	const float ctrl_y = start.y + ImMax(0.f, (label_h - ctrl_h) * 0.5f);
	ImGui::SetCursorPos({start.x + avail - control_width, ctrl_y});
	return start.y + label_h;
}

namespace
{
struct button_colors
{
	ImVec4 bg, bg_hovered, bg_active, text;
};

button_colors colors_for(button_style s, const theme & t)
{
	switch (s)
	{
		case button_style::primary:
			return {t.accent, t.accent_hovered, t.accent_active, t.on_accent};
		case button_style::secondary:
			return {t.control, t.control_hovered, t.control_active, t.text};
		case button_style::danger:
			return {t.danger, t.danger_hovered, t.danger, t.on_accent};
		case button_style::ghost:
			return {{0, 0, 0, 0}, t.control, t.control_active, t.text};
	}
	return {};
}
} // namespace

std::string icon_label(const char * icon, const std::string & label)
{
	return std::string(icon) + "  " + label;
}

float button_width(const std::string & label)
{
	return ImGui::CalcTextSize(label.c_str()).x + metrics::button_padding.x * 2;
}

float button_width(const char * icon, const std::string & label)
{
	ImGui::PushFont(nullptr, ImGui::GetFontSize() * metrics::button_label_glyph);
	const float icon_w = ImGui::CalcTextSize(icon).x;
	ImGui::PopFont();
	const float gap = ImGui::GetFontSize() * 0.4f;
	return icon_w + gap + ImGui::CalcTextSize(label.c_str()).x + metrics::button_padding.x * 2;
}

ImVec2 chip_size(const std::string & label)
{
	const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
	return {ts.x + metrics::chip_padding.x * 2, ts.y + metrics::chip_padding.y * 2};
}

float chip_width(const std::string & label, bool dot, float height)
{
	// mirrors the footprint computed in chip()
	const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
	const float pad_x = height > 0 ? metrics::chip_pill_padding_x : metrics::chip_padding.x;
	const float dot_r = ts.y * 0.26f;
	const float dot_gap = dot ? dot_r * 2 + pad_x * 0.55f : 0;
	return ts.x + pad_x * 2 + dot_gap;
}

bool button(const std::string & label, button_style s, const ImVec2 & size)
{
	const theme & t = current();

	// control height by default, width auto-fits the label
	const float h = size.y > 0 ? size.y : ImGui::GetFrameHeight() * metrics::control_height;

	const button_colors c = colors_for(s, t);
	ImGui::PushStyleColor(ImGuiCol_Button, c.bg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.bg_hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.bg_active);
	ImGui::PushStyleColor(ImGuiCol_Text, c.text);

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, t.rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, metrics::button_padding);
	const bool pressed = ImGui::Button(label.c_str(), {size.x, h});
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);
	hover_haptic();
	return pressed;
}

bool button(const char * icon, const std::string & label, button_style s, const ImVec2 & size)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const theme & t = current();

	const float h = size.y > 0 ? size.y : ImGui::GetFrameHeight() * metrics::control_height;
	const float glyph_size = ImGui::GetFontSize() * metrics::button_label_glyph;
	const float gap = ImGui::GetFontSize() * 0.4f;

	ImGui::PushFont(nullptr, glyph_size);
	const float icon_w = ImGui::CalcTextSize(icon).x;
	ImGui::PopFont();
	const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
	const float content_w = icon_w + gap + label_size.x;
	const float w = size.x > 0 ? size.x : content_w + metrics::button_padding.x * 2;

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + ImVec2{w, h});
	ImGui::ItemSize(bb, style.FramePadding.y);
	const ImGuiID id = window->GetID(label.c_str());
	if (not ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
	hover_haptic();

	const button_colors c = colors_for(s, t);
	ImVec4 bg = c.bg;
	if (held)
		bg = c.bg_active;
	else if (hovered)
		bg = c.bg_hovered;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), t.rounding);

	const ImU32 col = t.col(c.text);
	const ImVec2 center = bb.GetCenter();
	float x = center.x - content_w * 0.5f;

	// icon centred vertically on its ink box, like icon_button
	ImGui::PushFont(nullptr, glyph_size);
	unsigned int codepoint = 0;
	ImTextCharFromUtf8(&codepoint, icon, nullptr);
	const ImFontGlyph * glyph = ImGui::GetFontBaked()->FindGlyph((ImWchar)codepoint);
	if (glyph)
		draw->AddText({x, center.y - (glyph->Y0 + glyph->Y1) * 0.5f}, col, icon);
	else
		draw->AddText({x, center.y - glyph_size * 0.5f}, col, icon);
	ImGui::PopFont();

	x += icon_w + gap;
	draw->AddText({x, center.y - label_size.y * 0.5f}, col, label.c_str());

	return pressed;
}

bool icon_button(const char * icon, const ImVec2 & size_arg, bool active, const std::string & tooltip)
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

	// centre the glyph on its ink box, not CalcTextSize (which includes bearing/leading)
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

bool cancel_progress_button(const char * id, float fraction, const ImVec2 & size_arg, const std::string & tooltip)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const theme & t = current();

	const float side = ImGui::GetFrameHeight() * metrics::control_height;
	const ImVec2 size = size_arg.x > 0 ? size_arg : ImVec2{side, side};

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, g.Style.FramePadding.y);
	const ImGuiID gid = window->GetID(id);
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();
	if (hovered)
		show_tooltip(tooltip);

	ImDrawList * draw = window->DrawList;
	const ImVec2 center = bb.GetCenter();
	const float thickness = ImMax(2.f, size.y * 0.09f);
	const float radius = ImMin(size.x, size.y) * 0.5f - thickness;

	// faint full ring as the track
	draw->PathArcTo(center, radius, 0, 2 * IM_PI, 48);
	draw->PathStroke(t.col(t.control), ImDrawFlags_None, thickness);

	const ImU32 accent = t.col(t.accent);
	if (fraction >= 0)
	{
		// determinate arc, clockwise from the top
		const float a0 = -IM_PI * 0.5f;
		draw->PathArcTo(center, radius, a0, a0 + ImClamp(fraction, 0.f, 1.f) * 2 * IM_PI, 48);
		draw->PathStroke(accent, ImDrawFlags_None, thickness);
	}
	else
	{
		// indeterminate: a rotating quarter arc
		const float a0 = (float)g.Time * 3.5f;
		draw->PathArcTo(center, radius, a0, a0 + IM_PI * 0.5f, 24);
		draw->PathStroke(accent, ImDrawFlags_None, thickness);
	}

	// stop glyph in the centre, brightening on hover to read as a cancel target
	const float glyph_size = size.y * metrics::icon_button_glyph * 0.7f;
	ImGui::PushFont(nullptr, glyph_size);
	const ImU32 col = t.col(hovered or held ? t.text : t.text_muted);
	unsigned int codepoint = 0;
	ImTextCharFromUtf8(&codepoint, ICON_FA_STOP, nullptr);
	const ImFontGlyph * glyph = ImGui::GetFontBaked()->FindGlyph((ImWchar)codepoint);
	if (glyph)
		draw->AddText({center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f}, col, ICON_FA_STOP);
	ImGui::PopFont();

	return pressed;
}

// Horizontal space a trailing reset slot consumes, gap included
float reset_slot_width()
{
	return ImGui::GetFrameHeight() * metrics::control_height + ImGui::GetStyle().ItemSpacing.x;
}

// Trailing reset button on a value control's row; empty slot when show is false so the
// right edge stays put. Returns true when tapped.
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
	{
		// round the right corners too once the fill reaches the box edge, else they stay sharp over the rounded background
		const ImDrawFlags fill_flags = fill_x >= bb.Max.x - t.rounding ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
		draw->AddRectFilled(bb.Min, {fill_x, bb.Max.y}, t.col(t.accent_active), t.rounding, fill_flags);
	}
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
// Height of the popup layer; caps tall combo lists so they scroll. 0 = unbounded.
float popup_available_height = 0;

// One selectable option inside the combo modal. A tall, full-width touch target
// with the name and an optional muted description.
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
	const ImU32 name_col = t.col(t.text);
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

void set_popup_center(const ImVec2 & center, float available_height)
{
	popup_center = center;
	popup_available_height = available_height;
}

bool combo(const char * id, const std::string & title, const std::vector<combo_item> & items, int * selected, float width, const int * default_value)
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
		if (not title.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy({0, style.ItemSpacing.y});
		}

		// cap the list height so a long list scrolls inside the modal rather than
		// overflowing the popup layer, which would clip the top and bottom rows
		const float row_h = ImGui::GetFrameHeight() * metrics::combo_row_height;
		// rows are separated by ItemSpacing.y, so the real content is taller than the
		// row heights alone; include the gaps or a scrollbar shows even when it all fits
		float list_h = int(items.size()) * row_h + ImMax(0, int(items.size()) - 1) * style.ItemSpacing.y;
		if (popup_available_height > 0)
		{
			const float title_h = title.empty() ? 0 : ImGui::GetTextLineHeight() + style.ItemSpacing.y;
			const float max_list = popup_available_height * 0.9f - 2 * metrics::card_padding.y - title_h;
			list_h = ImMin(list_h, ImMax(max_list, row_h));
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		ImGui::BeginChild("##list", {0, list_h});
		for (int i = 0; i < int(items.size()); ++i)
		{
			const bool is_sel = i == sel;
			if (combo_row(items[i], is_sel))
			{
				*selected = i;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			if (is_sel and ImGui::IsWindowAppearing())
				ImGui::SetScrollHereY(0.5f); // reveal the current value when opening
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();

		// click outside dismisses; AllowWhenBlockedByActiveItem so pressing a row still counts as inside
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

void chip(const std::string & label, chip_style style, bool dot, float height)
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

	const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
	const ImVec2 pad = metrics::chip_padding;
	// tall pill chips (height override, e.g. the top bar) get roomier sides
	const float pad_x = height > 0 ? metrics::chip_pill_padding_x : pad.x;
	const float dot_r = ts.y * 0.26f;
	const float dot_gap = dot ? dot_r * 2 + pad_x * 0.55f : 0;
	const ImVec2 size = {ts.x + pad_x * 2 + dot_gap, height > 0 ? height : ts.y + pad.y * 2};

	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb);
	if (not ImGui::ItemAdd(bb, 0))
		return;

	ImDrawList * draw = window->DrawList;
	draw->AddRectFilled(bb.Min, bb.Max, t.col(bg), size.y * 0.5f);

	float tx = bb.Min.x + pad_x;
	if (dot)
	{
		draw->AddCircleFilled({tx + dot_r, bb.Min.y + size.y * 0.5f}, dot_r, t.col(fg));
		tx += dot_r * 2 + pad_x * 0.55f;
	}
	draw->AddText({tx, bb.Min.y + (size.y - ts.y) * 0.5f}, t.col(fg), label.c_str());
}

void nav_section(const std::string & label)
{
	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	ImGui::Dummy({0, metrics::nav_section_gap}); // gap above the section
	ImGui::Indent(metrics::combo_padding.x);     // align with the nav_item icons
	ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_description);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
	ImGui::TextUnformatted(label.c_str());
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::Unindent(metrics::combo_padding.x);
	ImGui::Dummy({0, 4});
}

bool nav_item(const char * icon, const std::string & label, bool selected)
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
	const ImGuiID gid = window->GetID(label.c_str());
	if (not ImGui::ItemAdd(bb, gid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
	hover_haptic();

	ImDrawList * draw = window->DrawList;
	const float lum = t.background.x * 0.299f + t.background.y * 0.587f + t.background.z * 0.114f;
	if (selected)
	{
		// Pure accent fill for the active tab.
		draw->AddRectFilled(bb.Min, bb.Max, t.col(t.accent), t.rounding);
	}
	else if (hovered)
	{
		// Subtle lighten on dark themes, darken on light ones. The framebuffer
		// blends in linear light, so a fixed alpha would look far stronger on a
		// dark surface than a light one. Solve for the alpha that yields a constant
		// *perceived* (gamma-space) step instead.
		const float step = lum < 0.5f ? 0.10f : 0.06f; // perceptual lightness delta; stronger on dark
		auto to_linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
		const float overlay = lum < 0.5f ? 1.f : 0.f; // lighten vs darken
		const float ys = to_linear(lum);
		const float yt = to_linear(std::clamp(lum + (overlay - lum) / std::abs(overlay - lum) * step, 0.f, 1.f));
		const float a = (yt - ys) / (overlay - ys);
		draw->AddRectFilled(bb.Min, bb.Max, t.col({overlay, overlay, overlay, a}), t.rounding);
	}

	const float pad = metrics::combo_padding.x;
	const float icon_w = ImGui::GetFontSize() * 1.5f; // icon column

	const ImVec2 its = ImGui::CalcTextSize(icon);
	draw->AddText({bb.Min.x + pad + (icon_w - its.x) * 0.5f, bb.Min.y + (h - its.y) * 0.5f}, t.col(selected ? t.on_accent : t.text_muted), icon);

	const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
	draw->AddText({bb.Min.x + pad + icon_w + pad * 0.5f, bb.Min.y + (h - ls.y) * 0.5f}, t.col(selected ? t.on_accent : t.text), label.c_str());

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

bool begin_modal(const char * id, const std::string & title, float width)
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
		if (not title.empty())
		{
			ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_modal_title);
			ImGui::TextUnformatted(title.c_str());
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

int confirm_modal(const char * id, const std::string & title, const std::string & message, const std::string & confirm_label, const std::string & cancel_label, bool danger)
{
	int result = 0;
	if (begin_modal(id, title, 520))
	{
		const theme & t = current();
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
		ImGui::TextWrapped("%s", message.c_str());
		ImGui::PopStyleColor();
		ImGui::Dummy({0, 12});

		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float cancel_w = ImGui::CalcTextSize(cancel_label.c_str()).x + metrics::button_padding.x * 2;
		const float confirm_w = ImGui::CalcTextSize(confirm_label.c_str()).x + metrics::button_padding.x * 2;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - cancel_w - confirm_w - gap);
		if (button(cancel_label, button_style::secondary, {cancel_w, 0}))
		{
			result = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (button(confirm_label, danger ? button_style::danger : button_style::primary, {confirm_w, 0}))
		{
			result = 1;
			ImGui::CloseCurrentPopup();
		}
		end_modal();
	}
	return result;
}

namespace
{
std::vector<ImVec2> list_row_after; // cursor to restore at end_list_row
}

list_row_result begin_list_row(const char * id, const char * icon, ImTextureID image, const std::string & title, const std::string & subtitle, bool selected, float trailing_width, float height, bool large_thumb, bool interactive)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const theme & t = current();
	const ImGuiStyle & style = ImGui::GetStyle();

	// measure title + (possibly multi-line) subtitle so the row can auto-size
	const float pad = metrics::list_row_pad;
	const float box = large_thumb ? metrics::list_row_box_large : metrics::list_row_box;
	const ImVec2 ts = ImGui::CalcTextSize(title.c_str());
	ImVec2 sub_sz{0, 0};
	if (not subtitle.empty())
	{
		ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_description);
		sub_sz = ImGui::CalcTextSize(subtitle.c_str());
		ImGui::PopFont();
	}
	const float text_h = ts.y + (subtitle.empty() ? 0 : metrics::label_line_gap + sub_sz.y);
	const float row_h = height > 0 ? height : ImMax(box, text_h) + 2 * pad;
	const ImVec2 p0 = window->DC.CursorPos;
	const float avail = ImGui::GetContentRegionAvail().x;

	// non-interactive rows (the body does nothing, only trailing controls act) just reserve
	// layout space -- no click area and no hover highlight on the row itself
	bool clicked = false;
	if (interactive)
	{
		// the click area excludes the trailing controls so they receive their own clicks
		ImGui::SetNextItemAllowOverlap();
		clicked = ImGui::InvisibleButton(id, {ImMax(avail - trailing_width, 1.f), row_h});
		hover_haptic();
	}
	else
		ImGui::Dummy({avail, row_h});
	const bool hovered = interactive and ImGui::IsItemHovered();
	list_row_after.push_back(window->DC.CursorPos);

	ImDrawList * draw = window->DrawList;
	const ImRect bb(p0, p0 + ImVec2(avail, row_h));
	// No tinted background for the selected row -- callers indicate the active item with a
	// chip instead. Still show the hover highlight.
	if (hovered)
		draw->AddRectFilled(bb.Min, bb.Max, t.col(t.control), t.card_rounding);

	// leading thumbnail or icon box; large_thumb spans the full row height
	const float thumb = large_thumb ? row_h - 2 * pad : box;
	const ImVec2 box_min = {p0.x + pad, p0.y + (row_h - thumb) * 0.5f};
	const ImVec2 box_max = box_min + ImVec2(thumb, thumb);
	if (image)
		draw->AddImageRounded(image, box_min, box_max, {0, 0}, {1, 1}, IM_COL32_WHITE, t.rounding);
	else
	{
		draw->AddRectFilled(box_min, box_max, t.col(selected ? t.accent : t.control), t.rounding);
		if (icon and icon[0])
		{
			const ImVec2 is = ImGui::CalcTextSize(icon);
			draw->AddText(box_min + (ImVec2(thumb, thumb) - is) * 0.5f, t.col(selected ? t.on_accent : t.text_muted), icon);
		}
	}

	// title + subtitle
	const float tx = box_max.x + pad;
	const float total = subtitle.empty() ? ts.y : ts.y + metrics::label_line_gap + sub_sz.y;
	const float ty = p0.y + (row_h - total) * 0.5f;
	draw->AddText({tx, ty}, t.col(t.text), title.c_str());
	if (not subtitle.empty())
	{
		ImGui::PushFont(nullptr, style.FontSizeBase * metrics::font_description);
		draw->AddText({tx, ty + ts.y + metrics::label_line_gap}, t.col(t.text_muted), subtitle.c_str());
		ImGui::PopFont();
	}

	// trailing controls right-align against the inner padding
	return {bb.Min, {bb.Max.x - pad, bb.Max.y}, clicked};
}

void end_list_row()
{
	ImGui::GetCurrentWindow()->DC.CursorPos = list_row_after.back();
	list_row_after.pop_back();
}

int action_menu(const char * id, const char * icon, const std::vector<action_item> & items)
{
	const theme & t = current();

	if (icon_button(icon, {}, false))
		ImGui::OpenPopup(id);

	int chosen = -1;

	const float pad = metrics::combo_padding.x;
	const float icon_w = ImGui::GetFontSize() * 1.5f;
	float content_w = 0;
	for (const auto & it: items)
		content_w = ImMax(content_w, icon_w + pad + ImGui::CalcTextSize(it.label).x + pad * 2 + ImGui::GetFontSize());

	// centred modal on the popup layer, like combo()
	ImGui::SetNextWindowPos(popup_center, ImGuiCond_Always, {0.5f, 0.5f});
	ImGui::SetNextWindowSize({ImMax(content_w, metrics::combo_modal_min_width), 0});

	ImGui::PushStyleColor(ImGuiCol_PopupBg, t.card);
	ImGui::PushStyleColor(ImGuiCol_Border, t.border);
	ImGui::PushStyleColor(ImGuiCol_Text, t.text);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, t.card_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, metrics::card_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, t.border_size > 0 ? t.border_size : 1);

	if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGuiWindow * window = ImGui::GetCurrentWindow();
		const float row_h = ImGui::GetFrameHeight() * metrics::control_height;

		for (int i = 0; i < int(items.size()); ++i)
		{
			const action_item & it = items[i];
			ImGui::PushID(i);
			const float row_w = ImGui::GetContentRegionAvail().x;
			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb(pos, pos + ImVec2(row_w, row_h));
			ImGui::ItemSize(bb, 0);
			const ImGuiID gid = window->GetID("##row");
			if (ImGui::ItemAdd(bb, gid))
			{
				bool hovered, held;
				if (ImGui::ButtonBehavior(bb, gid, &hovered, &held))
				{
					chosen = i;
					ImGui::CloseCurrentPopup();
				}
				hover_haptic();

				ImDrawList * draw = window->DrawList;
				if (hovered)
					draw->AddRectFilled(bb.Min, bb.Max, t.col(t.control_hovered), t.rounding);

				const ImU32 fg = t.col(it.danger ? t.danger : t.text);
				const ImVec2 its = ImGui::CalcTextSize(it.icon);
				draw->AddText({bb.Min.x + pad + (icon_w - its.x) * 0.5f, bb.Min.y + (row_h - its.y) * 0.5f}, fg, it.icon);
				const ImVec2 ls = ImGui::CalcTextSize(it.label);
				draw->AddText({bb.Min.x + pad + icon_w, bb.Min.y + (row_h - ls.y) * 0.5f}, fg, it.label);
				if (it.checked)
				{
					const ImVec2 cs = ImGui::CalcTextSize(ICON_FA_CHECK);
					draw->AddText({bb.Max.x - pad - cs.x, bb.Min.y + (row_h - cs.y) * 0.5f}, t.col(t.accent), ICON_FA_CHECK);
				}
			}
			ImGui::PopID();
		}

		// click outside dismisses; AllowWhenBlockedByActiveItem so pressing a row still counts as inside
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) and not ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(3);
	return chosen;
}

void top_bar(float height, ImTextureID logo, const std::vector<top_bar_item> & right_items)
{
	const float side = ImGui::GetFrameHeight() * metrics::control_height;
	const float gap = 8;
	const float margin = 24;

	// transparent child bg so the alpha-blended window background shows through
	ImGui::SetCursorPos({0, 0});
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
	ImGui::BeginChild("TopBar", {ImGui::GetWindowSize().x, height}, 0, ImGuiWindowFlags_NoScrollbar);
	{
		// logo: WiVRn mascot followed by the wordmark
		const float logo_size = 44;
		ImGui::SetCursorPos({margin, (height - logo_size) * 0.5f});
		if (logo)
			ImGui::Image(logo, {logo_size, logo_size});
		ImGui::SameLine(0, 12);
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
		ImGui::SetCursorPosY((height - ImGui::GetFontSize()) * 0.5f);
		ImGui::TextUnformatted("WiVRn");
		ImGui::PopFont();

		// right-aligned cluster
		float cluster_w = 0;
		for (const auto & it: right_items)
			cluster_w += it.width;
		if (not right_items.empty())
			cluster_w += gap * (right_items.size() - 1);

		ImGui::SetCursorPos({ImGui::GetWindowSize().x - margin - cluster_w, (height - side) * 0.5f});
		for (size_t i = 0; i < right_items.size(); ++i)
		{
			ImGui::SetCursorPosY((height - side) * 0.5f);
			right_items[i].draw();
			if (i + 1 < right_items.size())
				ImGui::SameLine(0, gap);
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void begin_sidebar(float top_bar_h, float tab_width)
{
	// shares the transparent window background; sections separated by shell_dividers
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
	ImGui::SetCursorPos({0, top_bar_h});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
	ImGui::BeginChild("Tabs", {tab_width, ImGui::GetWindowSize().y - top_bar_h}, ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 4});
}

void end_sidebar()
{
	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
	ImGui::EndChild();
	ImGui::PopStyleVar();   // ImGuiStyleVar_WindowPadding
	ImGui::PopStyleColor(); // ImGuiCol_ChildBg
}

void shell_dividers(float top_bar_h, float tab_width)
{
	ImVec4 divider_col = current().border;
	divider_col.w *= background_alpha(); // follow the panel opacity
	const ImU32 divider = ImGui::GetColorU32(divider_col);
	const ImVec2 win_pos = ImGui::GetWindowPos();
	const ImVec2 win_size = ImGui::GetWindowSize();
	ImDrawList * dl = ImGui::GetWindowDrawList();
	dl->AddLine({win_pos.x, win_pos.y + top_bar_h}, {win_pos.x + win_size.x, win_pos.y + top_bar_h}, divider);
	dl->AddLine({win_pos.x + tab_width, win_pos.y + top_bar_h}, {win_pos.x + tab_width, win_pos.y + win_size.y}, divider);
}

} // namespace wivrn::ui
