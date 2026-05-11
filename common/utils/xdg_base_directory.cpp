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

#include "strings.h"
#include <cstdlib>
#include <filesystem>

std::filesystem::path xdg_config_home()
{
	if (const char * xdg_config_home = std::getenv("XDG_CONFIG_HOME"))
		return xdg_config_home;

	if (const char * home = std::getenv("HOME"))
		return std::filesystem::path(home) / ".config";

	return ".";
}

std::filesystem::path xdg_cache_home()
{
	if (const char * xdg_cache_home = std::getenv("XDG_CACHE_HOME"))
		return xdg_cache_home;

	if (const char * home = std::getenv("HOME"))
		return std::filesystem::path(home) / ".cache";

	return ".";
}

std::filesystem::path xdg_data_home()
{
	if (const char * xdg_data_home = std::getenv("XDG_DATA_HOME"); xdg_data_home and *xdg_data_home)
		return xdg_data_home;

	if (const char * home = std::getenv("HOME"))
		return std::filesystem::path(home) / ".local/share";

	return ".";
}

std::vector<std::filesystem::path> xdg_config_dirs()
{
	std::vector<std::filesystem::path> paths;

	if (const char * xdg_config_dirs = std::getenv("XDG_CONFIG_DIRS"))
	{
		for (const auto & path: utils::split(xdg_config_dirs, ":"))
			paths.push_back(path);
	}
	else
	{
		paths.push_back("/etc/xdg");
	}

	return paths;
}

std::vector<std::filesystem::path> xdg_data_dirs(bool include_data_home)
{
	std::vector<std::filesystem::path> paths;
	if (include_data_home)
		paths.push_back(xdg_data_home());

	if (const char * xdg_config_dirs = std::getenv("XDG_DATA_DIRS"))
	{
		for (const auto & path: utils::split(xdg_config_dirs, ":"))
			paths.push_back(path);
	}
	else
	{
		paths.push_back("/usr/local/share");
		paths.push_back("/usr/share");
	}

	return paths;
}
