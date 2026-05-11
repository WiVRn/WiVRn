/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace wivrn
{

class steam
{
public:
	struct application
	{
		uint64_t appid;
		// localised names, with empty locale for default
		std::unordered_map<std::string, std::string> name;
		std::string url;
	};
	struct icon
	{
		std::string clienticon;
		std::string linuxclienticon;
	};

private:
	std::filesystem::path root;
	bool flatpak;
	std::optional<uint32_t> default_userid;
	std::optional<std::unordered_map<uint32_t, icon>> icons;
	std::unordered_map<uint32_t, std::filesystem::path> shortcut_icons;

	steam(std::filesystem::path root, bool flatpak);

public:
	static std::vector<steam> find_installations();

	std::vector<application> list_applications();
	std::optional<std::filesystem::path> get_icon(uint64_t app_id);
	std::string get_steam_command() const;
};

struct steam_shortcut
{
	uint32_t appid;
	std::string name;
	std::optional<std::filesystem::path> icon;
};

} // namespace wivrn
