/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "imgui.h"
#include "render/imgui_impl.h"
#include "wivrn_packets.h"
#include <chrono>
#include <unordered_map>

namespace scenes
{
class stream;
}

class app_launcher
{
	struct app
	{
		std::string id;
		std::string name;
		std::vector<std::byte> image;
	};

	std::chrono::steady_clock::time_point start_time{};
	std::string server_name;
	scenes::stream & stream;
	imgui_textures textures;
	ImTextureID default_icon;
	std::unordered_map<std::string, ImTextureID> app_icons;
	// Last application list received from server
	thread_safe<std::vector<app>> applications;

public:
	app_launcher(scenes::stream &, std::string server_name);
	~app_launcher();
	enum clicked
	{
		None,
		Cancel,
		Start,
	};
	// cancel: text to display on the quit/disconnect/cancel button
	clicked draw_gui(imgui_context &, const std::string & cancel);

	void operator()(wivrn::to_headset::application_list && apps);
	void operator()(wivrn::to_headset::application_icon && icon);
};
