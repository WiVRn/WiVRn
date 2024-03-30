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

#include <cstdlib>
#include <filesystem>

std::filesystem::path xdg_config_home()
{
	const char * xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	if (xdg_config_home)
		return xdg_config_home;
	const char * home = std::getenv("HOME");
	if (home)
		return std::filesystem::path(home) / ".config";
	return ".";
}

std::filesystem::path xdg_cache_home()
{
	const char * xdg_cache_home = std::getenv("XDG_CACHE_HOME");
	if (xdg_cache_home)
		return xdg_cache_home;
	const char * home = std::getenv("HOME");
	if (home)
		return std::filesystem::path(home) / ".cache";
	return ".";
}
