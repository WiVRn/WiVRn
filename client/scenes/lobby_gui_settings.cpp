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

// lobby-side settings pages, shared with the in-stream window (see gui_settings.cpp)

#include "lobby.h"

#include "application.h"
#include "gui_settings.h"

namespace
{
wivrn::gui::settings_context lobby_settings_context(xr::instance & instance, xr::session & session, xr::system & system, imgui_context & imgui_ctx, const XrViewConfigurationView & stream_view, std::optional<bool> server_hid_forwarding)
{
	return {
	        .config = application::get_config(),
	        .instance = instance,
	        .session = session,
	        .system = system,
	        .imgui_ctx = imgui_ctx,
	        .recommended_width = stream_view.recommendedImageRectWidth,
	        .recommended_height = stream_view.recommendedImageRectHeight,
	        .in_game = false,
	        .server_hid_forwarding = server_hid_forwarding,
	};
}
} // namespace

void scenes::lobby::gui_performance()
{
	wivrn::gui::settings_performance(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_streaming()
{
	wivrn::gui::settings_streaming(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_post_processing()
{
	wivrn::gui::settings_post_processing(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_audio()
{
	wivrn::gui::settings_audio(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_devices()
{
	wivrn::gui::settings_devices(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_tracking()
{
	wivrn::gui::settings_tracking(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}

void scenes::lobby::gui_system()
{
	wivrn::gui::settings_system(lobby_settings_context(instance, session, system, *imgui_ctx, stream_view, server_hid_forwarding));
}
