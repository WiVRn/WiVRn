/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <imgui.h>
#include <openxr/openxr.h>

namespace constants::gui
{
// Minimum distance between a GUI layer and a fingertip/controller to register a click
constexpr float min_pointer_distance = -0.02;

// Font to use (desktop only)
constexpr const char font_name[] = "Noto Sans";

// Font sizes
constexpr int font_size_small = 30;
constexpr int font_size_large = 75;

// Ratio between joystick position and scroll distance/s
constexpr float scroll_ratio = 10;

// Threshold on the trigger value to register a click
constexpr float trigger_click_thd = 0.8;

// Thresholds on the distance between the fingertip and GUI layers (positive: in front of the GUI, negative: behind the GUI)
constexpr float fingertip_distance_hovering_thd = 0.15;  // Max distance to have the cursor hovering
constexpr float fingertip_distance_touching_thd = -0.01; // Max distance to register a click
constexpr float fingertip_distance_stick_thd = -0.02;    // Max distance where the fingertip is moved to be on the GUI

// Minimum scroll value to enable a controller
constexpr float scroll_value_thd = 0.01;

// Pointer radius
constexpr float pointer_radius_in = 10;
constexpr float pointer_radius_out = 12;
constexpr float pointer_thickness = 4;

// Pointer transparency
constexpr float pointer_alpha = 0.8;
constexpr float pointer_alpha_disabled = 0.25;
constexpr float pointer_fading_distance = 40;

// Pointer color
constexpr uint32_t pointer_color_pressed = 0xffff3300;
constexpr uint32_t pointer_color_unpressed = 0xffffffff;
constexpr uint32_t pointer_color_border = 0xff000000;
} // namespace constants::gui

namespace constants::lobby
{
// Position and orientation of the GUI layers
constexpr auto gui_pitches = std::to_array<std::pair<float, float>>({
        {-90, -90},
        {-50, -90},
        {-30, -12},
        {30, -12},
        {50, 78},
        {90, 78},
});
constexpr float keyboard_pitch = -0.6;
constexpr glm::vec3 popup_position = {0, 0, 0.05};
constexpr glm::vec3 keyboard_position = {0, -0.3, 0.1};

// Position of the near plane
constexpr float near_plane = 0.02;

// Recenter gesture thresholds
constexpr float recenter_cos_palm_angle_min = 0.7;
constexpr float recenter_cos_fingertip_angle_max = 0.3;
constexpr float recenter_distance_up = 0.3;
constexpr float recenter_distance_front = 0.2;

// Recenter distance when using the controller, if the controller doesn't point at the GUI when the button is pressed
constexpr float recenter_action_distance = 0.3;

// Default distance between the headset and the GUI, when the GUI is first shown, when the session state changes, when the lobby is refocused
constexpr float initial_gui_distance = 0.5;

// Skybox color
constexpr XrColor4f sky_color = {0, 0.25, 0.5, 1};

// Dimming scale/bias when a popup window is shown
constexpr XrColor4f dimming_scale = {0.5, 0.5, 0.5, 1};
constexpr XrColor4f dimming_bias = {0.25, 0.25, 0.25, 0};

// Z-indices of composition layers
constexpr int zindex_passthrough = -2;
constexpr int zindex_lobby = -1;
constexpr int zindex_gui = 0;
constexpr int zindex_controllers = 1;
constexpr int zindex_recenter_tip = 2;
} // namespace constants::lobby

namespace constants::stream
{
constexpr float fade_delay = 3;
constexpr float fade_duration = 0.25;

// Dimming for the streamed video when the GUI is interactable
constexpr float dimming_scale = 0.7;
constexpr float dimming_bias = 0.15;
} // namespace constants::stream

namespace constants::style
{
constexpr ImVec2 window_padding = {20, 20};
constexpr float window_border_size = 2;
constexpr float window_rounding = 10;

constexpr ImVec2 tooltip_padding = {5, 5};
constexpr float tooltip_rounding = 0;
constexpr float tooltip_distance = 10;

constexpr ImVec2 button_size = {220, 80};
constexpr ImVec2 icon_button_size = {80, 80};
constexpr float connection_popup_width = 1000;

constexpr ImVec2 pin_entry_key_size = {90, 70};
constexpr ImVec2 pin_entry_item_spacing = {10, 10};
constexpr float pin_entry_popup_width = 3 * pin_entry_key_size.x + 2 * pin_entry_item_spacing.x;

} // namespace constants::style
