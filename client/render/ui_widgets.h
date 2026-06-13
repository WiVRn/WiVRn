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

#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

// Themed, reusable UI widgets. Colors and metrics come from wivrn::ui::current().
// Display text is taken by const std::string &; ids and icon glyphs stay const char *.
namespace wivrn::ui
{

enum class button_style
{
	primary,   // accent fill
	secondary, // control fill
	danger,    // destructive
	ghost,     // transparent until hovered
};

// Page title with an optional muted subtitle
void page_header(const std::string & title, const std::string & subtitle = {});

// Rounded panel, pair with end_card
bool begin_card(const char * id, const ImVec2 & size = {0, 0});
// Same, but with tighter padding for cards that hold a list of rows
bool begin_list_card(const char * id);
void end_card();

// Full-width divider between rows of a card
void row_separator();

// Title and wrapped muted description on the left, control_width reserved on the right.
// Returns the window-local y of the label bottom, so the caller can keep the row tall
// enough for a multi-line label.
float setting_label(const std::string & title, const std::string & description, float control_width);

// "<icon>  <label>" with the standard two-space gap, for chips and text buttons that
// take a single label string with an embedded glyph.
std::string icon_label(const char * icon, const std::string & label);

// Auto-fit width of a button, matching what button() lays out when size.x is left at 0.
// Use to reserve/right-align a button before drawing it. The icon overload accounts for
// the leading glyph drawn at metrics::button_label_glyph.
float button_width(const std::string & label);
float button_width(const char * icon, const std::string & label);

// Footprint of a chip() with standard padding (no dot, no height override).
ImVec2 chip_size(const std::string & label);

bool button(const std::string & label, button_style style = button_style::primary, const ImVec2 & size = {0, 0});
// Same, with a larger leading icon glyph drawn before the label
bool button(const char * icon, const std::string & label, button_style style = button_style::primary, const ImVec2 & size = {0, 0});
// tooltip, if not empty, is shown while hovered
bool icon_button(const char * icon, const ImVec2 & size = {0, 0}, bool active = false, const std::string & tooltip = {});

// Circular download-progress indicator with a cancel (stop) glyph in the centre.
// fraction in [0,1] draws a determinate ring; < 0 draws an indeterminate spinner.
// Returns true when clicked (i.e. cancel requested). Sized like icon_button by default.
bool cancel_progress_button(const char * id, float fraction, const ImVec2 & size = {0, 0}, const std::string & tooltip = {});

bool toggle(const char * id, bool * v, const bool * default_value = nullptr);

// Width a trailing reset slot consumes (gap + square), for laying out toggles
float reset_slot_width();

// The value controls below draw a reset button when the value differs from default_value
bool slider_int(const char * id, int * v, int v_min, int v_max, const char * format, const ImVec2 & size = {0, 0}, const int * default_value = nullptr);

// Pill group, *selected is the index of the active option
bool segmented(const char * id, const std::vector<std::string> & options, int * selected, const ImVec2 & size = {0, 0}, const int * default_value = nullptr);

struct combo_item
{
	const char * name;
	const char * description = nullptr;
};

// Combo box whose options open in a modal list on the popup layer. *selected is the
// chosen index, title labels the modal. Returns true when the selection changes.
bool combo(const char * id, const std::string & title, const std::vector<combo_item> & items, int * selected, float width = 0, const int * default_value = nullptr);

// Centre (display coords) where combo and begin_modal popups open, plus the height
// available on the popup layer so tall combo lists cap and scroll instead of being
// clipped. available_height of 0 leaves the list unbounded. Set once per frame.
void set_popup_center(const ImVec2 & center, float available_height = 0);

// Hook fired after an interactive item, for a hover haptic. Install once.
void set_hover_haptic(std::function<void()> hook);

// Hook to show a tooltip for the last item. Install once.
void set_tooltip_hook(std::function<void(const char *)> hook);

enum class chip_style
{
	neutral, // control fill, normal text
	muted,   // control fill, muted text
	accent,
	success,
	warning,
	danger,
};
// Status badge. label may include a leading icon glyph; dot draws a status dot.
// height overrides the pill height (default sizes to the text + padding)
void chip(const std::string & label, chip_style style = chip_style::neutral, bool dot = false, float height = 0);

// Muted sidebar group header
void nav_section(const std::string & label);

// Sidebar entry: icon + label, highlighted when selected. Returns true when clicked
bool nav_item(const char * icon, const std::string & label, bool selected);

// Single-line text field at control height; hint is optional placeholder text
bool input_text(const char * id, std::string & text, const char * hint = nullptr, float width = 0);

// Number field with - / + stepper buttons. Returns true when the value changes
bool input_int(const char * id, int * v, int step = 1, int v_min = 0, int v_max = 0, float width = 0);

// Centred modal on the popup layer. Open with ImGui::OpenPopup(id), dismiss with
// ImGui::CloseCurrentPopup(); pair with end_modal only when this returns true.
bool begin_modal(const char * id, const std::string & title, float width = 0);
void end_modal();

struct action_item
{
	const char * icon;
	const char * label;
	bool danger = false;
	bool checked = false;
};

// Overflow button: an icon button opening a popup of actions. Returns the picked index, or -1
int action_menu(const char * id, const char * icon, const std::vector<action_item> & items);

struct list_row_result
{
	ImVec2 min, max; // row rect (screen) for right-aligning trailing controls
	bool clicked;    // the row body was clicked

	// Screen position for a trailing control of the given size, right-aligned to right_x
	// and vertically centred in the row. Feed to ImGui::SetCursorScreenPos.
	ImVec2 trailing(float right_x, const ImVec2 & size) const
	{
		return {right_x - size.x, min.y + (max.y - min.y - size.y) * 0.5f};
	}
};

// List row: leading thumbnail (image, if non-zero) or icon box, title and muted subtitle.
// trailing_width reserves space on the right for trailing controls and excludes it from
// the row's click area. Place controls right-aligned at .max with SetCursorScreenPos,
// then call end_list_row().
// large_thumb makes the leading thumbnail/icon box span the full row height (icon boxes
// keep their background at that same size).
// interactive=false reserves layout only -- no row-body click area and no hover highlight,
// for rows whose body does nothing (only trailing controls act).
list_row_result begin_list_row(const char * id, const char * icon, ImTextureID image, const std::string & title, const std::string & subtitle, bool selected = false, float trailing_width = 0, float height = 0, bool large_thumb = false, bool interactive = true);
void end_list_row();

// Confirmation dialog (open with ImGui::OpenPopup(id)). Returns 1 if confirmed, -1 if
// cancelled or dismissed, 0 otherwise. danger styles the confirm button as destructive.
int confirm_modal(const char * id, const std::string & title, const std::string & message, const std::string & confirm_label, const std::string & cancel_label, bool danger = false);

} // namespace wivrn::ui
