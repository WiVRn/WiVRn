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

#include "active_runtime.h"
#include "utils/xdg_base_directory.h"
#include "wivrn_config.h"
#include <filesystem>
#include <fstream>
#include <iostream>

std::filesystem::path active_runtime::manifest_path()
{
	const std::filesystem::path install_location = "share/openxr/1/openxr_wivrn.json";
	// Check if in a flatpak
	if (std::filesystem::exists("/.flatpak-info"))
	{
		const std::string key("app-path=");
		std::string line;
		std::ifstream info("/.flatpak-info");
		while (std::getline(info, line))
		{
			if (line.starts_with(key))
				return line.substr(key.size()) / install_location.relative_path();
		}
	}

	// Check if running from build directory
	auto exe = std::filesystem::read_symlink("/proc/self/exe");
	auto dev_manifest = exe.parent_path().parent_path() / "openxr_wivrn-dev.json";
	if (std::filesystem::exists(dev_manifest))
		return dev_manifest;

	// Assume we are installed
	return std::filesystem::path(WIVRN_INSTALL_PREFIX) / install_location;
}

active_runtime::active_runtime() :
        pid(getpid()), active_runtime_json(xdg_config_home() / "openxr/1/active_runtime.json")
{
	try
	{
		if (std::filesystem::exists(active_runtime_json))
			return;

		std::filesystem::create_directories(active_runtime_json.parent_path());

		std::filesystem::create_symlink(manifest_path(), active_runtime_json);

		to_be_deleted = true;
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot set active OpenXR runtime: " << e.what() << std::endl;
	}
}

active_runtime::~active_runtime()
{
	try
	{
		if (pid == getpid() && to_be_deleted)
		{
			std::filesystem::remove(active_runtime_json);
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot unset active OpenXR runtime: " << e.what() << std::endl;
	}
}
