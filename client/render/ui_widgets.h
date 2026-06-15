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

// Themed, reusable UI widgets, colors and metrics from wivrn::ui::current()
// Display text takes const std::string &, ids and icon glyphs stay const char *
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
// Tighter padding, for cards holding a list of rows
bool begin_list_card(const char * id);
void end_card();

// Full-width divider between rows of a card
void row_separator();

// Title and wrapped muted description on the left, control_width reserved on the right
// Returns the window-local y of the label bottom, to keep the row tall enough for a multi-line label
float setting_label(const std::string & title, const std::string & description, float control_width);

// "<icon>  <label>" with the standard two-space gap
std::string icon_label(const char * icon, const std::string & label);

// Auto-fit width of a button, as laid out when size.x is 0
float button_width(const std::string & label);
float button_width(const char * icon, const std::string & label);

// Footprint of a chip() with standard padding, no dot, no height override
ImVec2 chip_size(const std::string & label);

// Exact layout width of a chip(), accounting for the optional dot and the height override padding
float chip_width(const std::string & label, bool dot = false, float height = 0);

bool button(const std::string & label, button_style style = button_style::primary, const ImVec2 & size = {0, 0});
// Same, with a larger leading icon glyph
bool button(const char * icon, const std::string & label, button_style style = button_style::primary, const ImVec2 & size = {0, 0});
// tooltip, if not empty, is shown while hovered
bool icon_button(const char * icon, const ImVec2 & size = {0, 0}, bool active = false, const std::string & tooltip = {});

// Circular download-progress indicator with a centred cancel glyph
// fraction in [0,1] draws a determinate ring, < 0 an indeterminate spinner
// Returns true when cancel is clicked
bool cancel_progress_button(const char * id, float fraction, const ImVec2 & size = {0, 0}, const std::string & tooltip = {});

bool toggle(const char * id, bool * v, const bool * default_value = nullptr);

// Width a trailing reset slot consumes, gap + square
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

// Combo box whose options open in a modal list, *selected is the chosen index, title labels the modal
bool combo(const char * id, const std::string & title, const std::vector<combo_item> & items, int * selected, float width = 0, const int * default_value = nullptr);

// Centre (display coords) where combo and begin_modal popups open, plus the popup-layer height
// available_height of 0 leaves the list unbounded, set once per frame
void set_popup_center(const ImVec2 & center, float available_height = 0);

// hover-haptic hook, fired after an interactive item
void set_hover_haptic(std::function<void()> hook);

// tooltip hook for the last item
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
// Status badge, label may include a leading icon glyph, dot draws a status dot
// height overrides the pill height, default sizes to text + padding
void chip(const std::string & label, chip_style style = chip_style::neutral, bool dot = false, float height = 0);

// Muted sidebar group header
void nav_section(const std::string & label);

// sidebar entry: icon + label, highlighted when selected, returns true when clicked
bool nav_item(const char * icon, const std::string & label, bool selected);

// Single-line text field at control height, hint is optional placeholder text
bool input_text(const char * id, std::string & text, const char * hint = nullptr, float width = 0);

// number field with -/+ steppers, returns true on change
bool input_int(const char * id, int * v, int step = 1, int v_min = 0, int v_max = 0, float width = 0);

// Centred modal on the popup layer, open with ImGui::OpenPopup(id), dismiss with ImGui::CloseCurrentPopup()
// Pair with end_modal only when this returns true
bool begin_modal(const char * id, const std::string & title, float width = 0);
void end_modal();

struct action_item
{
	const char * icon;
	const char * label;
	bool danger = false;
	bool checked = false;
};

// overflow icon button opening a popup of actions, returns the picked index or -1
int action_menu(const char * id, const char * icon, const std::vector<action_item> & items);

struct list_row_result
{
	ImVec2 min, max; // row rect (screen) for right-aligning trailing controls
	bool clicked;    // the row body was clicked

	// Screen position for a size trailing control, right-aligned to right_x and vertically centred
	ImVec2 trailing(float right_x, const ImVec2 & size) const
	{
		return {right_x - size.x, min.y + (max.y - min.y - size.y) * 0.5f};
	}
};

// List row: leading thumbnail (image, if non-zero) or icon box, title and muted subtitle
// trailing_width reserves right-side space for trailing controls, excluded from the click area
// large_thumb spans the leading thumbnail/icon box across the full row height
// interactive=false reserves layout only, no row-body click area or hover highlight
list_row_result begin_list_row(const char * id, const char * icon, ImTextureID image, const std::string & title, const std::string & subtitle, bool selected = false, float trailing_width = 0, float height = 0, bool large_thumb = false, bool interactive = true);
void end_list_row();

// confirmation dialog, open with ImGui::OpenPopup(id), returns 1 confirmed / -1 cancelled / 0 otherwise
// danger styles the confirm button as destructive
int confirm_modal(const char * id, const std::string & title, const std::string & message, const std::string & confirm_label, const std::string & cancel_label, bool danger = false);

// shared window shell: top bar, sidebar and dividers, called inside the main window

// One slot in the top bar's right-aligned cluster, width is the slot's layout width
// draw() renders it at the cursor, vertically centred, at the standard control height
struct top_bar_item
{
	float width;
	std::function<void()> draw;
};

// Top bar: WiVRn logo + wordmark on the left, right_items right-aligned, height in pixels
void top_bar(float height, ImTextureID logo, const std::vector<top_bar_item> & right_items);

// sidebar nav frame: scrolling top section, footer_items rows pinned at the bottom
// always pair: begin_sidebar -> sidebar_footer -> end_sidebar
void begin_sidebar(float top_bar_h, float tab_width, int footer_items = 0);
void sidebar_footer();
void end_sidebar();

// Hairline dividers separating the top bar, sidebar and content, dimmed with the panel
void shell_dividers(float top_bar_h, float tab_width);

} // namespace wivrn::ui
