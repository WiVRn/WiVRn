/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "flatpak.h"

#include <filesystem>
#include <fstream>

static const std::filesystem::path info_path = "/.flatpak-info";

bool wivrn::is_flatpak()
{
	return std::filesystem::exists(info_path);
}

std::optional<std::string> wivrn::flatpak_key(std::string key)
{
	key += "=";
	std::string line;
	std::ifstream info(info_path);
	while (std::getline(info, line))
	{
		if (line.starts_with(key))
			return line.substr(key.size());
	}
	return {};
}
