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

// Themed, reusable UI widgets matching the WiVRn dashboard design.
// All widgets read colors and metrics from wivrn::ui::current().
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
void page_header(const char * title, const char * subtitle = nullptr);

// Rounded panel, pair with end_card. Returns false when fully clipped
bool begin_card(const char * id, const ImVec2 & size = {0, 0});
void end_card();

// Full-width divider between rows of a card
void row_separator();

// Bold title + wrapped muted description in a left column,
// leaving control_width on the right for a right-aligned control
void setting_label(const char * title, const char * description, float control_width);

bool button(const char * label, button_style style = button_style::primary, const ImVec2 & size = {0, 0});
// tooltip, if given, is shown while the button is hovered
bool icon_button(const char * icon, const ImVec2 & size = {0, 0}, bool active = false, const char * tooltip = nullptr);

bool toggle(const char * id, bool * v, const bool * default_value = nullptr);

// Value controls below take an optional default_value. When given and the current
// value differs from it, a reset icon button is drawn in a trailing slot; tapping it
// restores the default. When the value matches (or no default is given) the slot is
// left empty so the control's right edge stays put.

// Filled bar slider with the value centered on the rail
bool slider_int(const char * id, int * v, int v_min, int v_max, const char * format, const ImVec2 & size = {0, 0}, const int * default_value = nullptr);

// Pill group, *selected is the index of the active option
bool segmented(const char * id, const std::vector<std::string> & options, int * selected, const ImVec2 & size = {0, 0}, const int * default_value = nullptr);

// One option of a combo: a label and an optional muted description
struct combo_item
{
	const char * name;
	const char * description = nullptr;
};

// Combo box whose options open in a modal list centred on the popup layer,
// instead of an inline dropdown. *selected is the chosen index, title labels
// the modal. Returns true when the selection changes.
bool combo(const char * id, const char * title, const std::vector<combo_item> & items, int * selected, float width = 0, const int * default_value = nullptr);

// Centre (in imgui display coords) where combo modals open. Set once per frame
// from the lobby's popup layer before any combo() is drawn.
void set_popup_center(const ImVec2 & center);

// Hook the widgets call after submitting an interactive item so it can fire a hover
// haptic (e.g. imgui_context::vibrate_on_hover). Install once from the lobby.
void set_hover_haptic(std::function<void()> hook);

// Hook a widget calls to show a tooltip for the last submitted item (e.g.
// imgui_context::tooltip). Only called while the item is hovered. Install once.
void set_tooltip_hook(std::function<void(const char *)> hook);

// Small status badge. The label may include a leading icon glyph; pass dot=true for
// a leading status dot in the style colour. Non-interactive.
enum class chip_style
{
	neutral, // control fill, normal text
	muted,   // control fill, muted text
	accent,
	success,
	warning,
	danger,
};
void chip(const char * label, chip_style style = chip_style::neutral, bool dot = false);

// Muted sidebar group header (e.g. "SETTINGS")
void nav_section(const char * label);

// Sidebar entry: icon + label across the full width, highlighted when selected.
// Returns true when clicked.
bool nav_item(const char * icon, const char * label, bool selected);

// Themed single-line text field, control height. hint is optional placeholder text.
// Returns true while the text is being edited.
bool input_text(const char * id, std::string & text, const char * hint = nullptr, float width = 0);

// Number field with - / + stepper buttons. Returns true when the value changes.
bool input_int(const char * id, int * v, int step = 1, int v_min = 0, int v_max = 0, float width = 0);

// Centered modal dialog on the popup layer, themed like a card with a title. Open it
// with ImGui::OpenPopup(id); call ImGui::CloseCurrentPopup() to dismiss. Pair with
// end_modal only when this returns true, exactly like ImGui::BeginPopupModal.
bool begin_modal(const char * id, const char * title, float width = 0);
void end_modal();

} // namespace wivrn::ui
