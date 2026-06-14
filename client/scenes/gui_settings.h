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

#include <cstdint>
#include <functional>

class configuration;
class imgui_context;

namespace xr
{
class instance;
class session;
class system;
} // namespace xr

namespace wivrn::gui
{

// what the shared settings pages need, in the lobby or the in-stream window
struct settings_context
{
	configuration & config;
	xr::instance & instance;
	xr::session & session;
	xr::system & system;
	imgui_context & imgui_ctx;

	// recommended per-eye render resolution, for the resolution description
	uint32_t recommended_width = 0;
	uint32_t recommended_height = 0;

	// streaming: connection-time settings are disabled, in-stream controls appear
	bool in_game = false;

	std::optional<bool> server_hid_forwarding;

	// in-stream hooks, empty in the lobby
	std::function<void()> on_streaming_changed;          // refresh rate / spacewarp / bitrate
	std::function<void()> enter_bitrate_adjust;          // thumbstick bitrate sub-mode
	std::function<void()> enter_foveation_adjust;        // thumbstick foveation sub-mode
	std::function<void()> on_foveation_override_changed; // foveation override toggled
};

void settings_performance(const settings_context &);
void settings_streaming(const settings_context &);
void settings_post_processing(const settings_context &);
void settings_audio(const settings_context &);
void settings_devices(const settings_context &);
bool settings_tracking(const settings_context &);
void settings_system(const settings_context &);

} // namespace wivrn::gui
