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

#pragma once

#include <imgui.h>
#include <string>
#include <vector>

namespace wivrn::ui
{

struct theme
{
	std::string name;

	// Accent
	ImVec4 accent;
	ImVec4 accent_hovered;
	ImVec4 accent_active;
	ImVec4 on_accent; // text/icon on an accent fill

	// Surfaces, back to front
	ImVec4 background;
	ImVec4 card;
	ImVec4 card_hovered;
	ImVec4 control; // slider rail, secondary button, input
	ImVec4 control_hovered;
	ImVec4 control_active;

	// Content
	ImVec4 text;
	ImVec4 text_muted;
	ImVec4 border;

	// Semantic
	ImVec4 danger;
	ImVec4 danger_hovered;
	ImVec4 success;
	ImVec4 warning;

	// Shape, in pixels
	float rounding;      // buttons, inputs, sliders
	float card_rounding; // cards
	float border_size;

	float font_scale;       // global text size multiplier
	float background_alpha; // opacity of the main panel background, 0..1

	// Convert to packed color, applying the current global alpha so widgets drawn
	// through the draw list dim with ImGui::BeginDisabled() (and any pushed alpha).
	ImU32 col(const ImVec4 & c) const
	{
		ImVec4 v = c;
		v.w *= ImGui::GetStyle().Alpha;
		return ImGui::ColorConvertFloat4ToU32(v);
	}

	// Push colors and metrics into the global ImGui style
	void apply() const;
};

// Fixed design tokens shared by the widgets
namespace metrics
{
constexpr float font_base = 0.765;       // design default text scale, i.e. what the user-facing 100% maps to
constexpr float font_title = 1.7;        // page header, x base font size
constexpr float font_description = 0.82; // muted secondary text, x base font size

constexpr ImVec2 button_padding = {26, 14};
constexpr ImVec2 card_padding = {28, 22};

// Shared height of every full-width control (slider, segmented, combo, input)
// so they line up in a setting row, in multiples of ImGui::GetFrameHeight()
constexpr float control_height = 1.65;

constexpr float toggle_aspect = 1.9;   // track width / height
constexpr float toggle_knob_inset = 3; // gap between track edge and knob

constexpr float slider_grab_width = 14;

constexpr float segmented_inset = 3; // gap around the active segment

constexpr float icon_button_glyph = 0.5; // glyph height as a fraction of the button side

constexpr float combo_row_height = 2.2;      // x frame height, big touch rows in the modal
constexpr float combo_chevron = 12;          // chevron size in the closed box
constexpr float combo_modal_min_width = 460; // popup never narrower than this
constexpr ImVec2 combo_padding = {16, 0};    // horizontal inner padding of box and rows

constexpr float list_row_box = 52;       // leading icon/thumbnail size in a list row
constexpr float list_row_pad = 16;       // inner padding of a list row, both axes
constexpr ImVec2 chip_padding = {12, 6}; // inner padding of a chip/badge
constexpr float input_padding_x = 14;    // horizontal text padding inside inputs
constexpr float font_modal_title = 1.2;  // modal heading, x base font size

constexpr float label_line_gap = 5;   // title to description
constexpr float label_bottom_pad = 6; // breathing room below the description
constexpr float nav_section_gap = 18; // space above a sidebar section header
} // namespace metrics

// Theme in effect, call current().apply() after mutating
theme & current();
void set_theme(const theme & t);

std::vector<theme> presets();

struct accent_swatch
{
	const char * name;
	ImVec4 base;
	ImVec4 hovered;
	ImVec4 active;
};
std::vector<accent_swatch> accent_swatches();
void set_accent(const accent_swatch & swatch);

} // namespace wivrn::ui
