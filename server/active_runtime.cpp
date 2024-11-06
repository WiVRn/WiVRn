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
#include "utils/flatpak.h"
#include "utils/xdg_base_directory.h"
#include "wivrn_config.h"
#include <filesystem>
#include <iostream>

namespace wivrn
{

std::filesystem::path active_runtime::manifest_path()
{
	const std::filesystem::path install_location = "share/openxr/1/openxr_wivrn.json";
	// Check if in a flatpak
	if (auto path = flatpak_key("app-path"))
		return *path / install_location.relative_path();

	// Check if running from build directory
	auto exe = std::filesystem::read_symlink("/proc/self/exe");
	auto dev_manifest = exe.parent_path().parent_path() / "openxr_wivrn-dev.json";
	if (std::filesystem::exists(dev_manifest))
		return dev_manifest;

	// Assume we are installed
	return std::filesystem::path(WIVRN_INSTALL_PREFIX) / install_location;
}

static std::filesystem::path backup_name(std::filesystem::path file)
{
	file += ".wivrn-backup";
	return file;
}

static void move_file(const std::filesystem::path & from, const std::filesystem::path & to)
{
	if (not std::filesystem::exists(from))
		return;
	std::filesystem::rename(from, to);
}

active_runtime::active_runtime() :
        active_runtime_json(xdg_config_home() / "openxr/1/active_runtime.json"), pid(getpid())
{
	try
	{
		std::filesystem::create_directories(active_runtime_json.parent_path());
		move_file(active_runtime_json, backup_name(active_runtime_json));

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
			move_file(backup_name(active_runtime_json), active_runtime_json);
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot unset active OpenXR runtime: " << e.what() << std::endl;
	}
}
} // namespace wivrn
