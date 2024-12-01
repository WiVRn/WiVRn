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
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace wivrn
{

std::filesystem::path active_runtime::manifest_path()
{
	const std::filesystem::path install_location = "share/openxr/1/openxr_wivrn.json";
	// Check if in a flatpak
	if (auto path = flatpak_key(flatpak::section::instance, "app-path"))
		return *path / install_location.relative_path();

	// Check if running from build directory
	auto exe = std::filesystem::read_symlink("/proc/self/exe");
	auto dev_manifest = exe.parent_path().parent_path() / "openxr_wivrn-dev.json";
	if (std::filesystem::exists(dev_manifest))
		return dev_manifest;

	// Assume we are installed
	return std::filesystem::path(WIVRN_INSTALL_PREFIX) / install_location;
}

std::filesystem::path active_runtime::opencomposite_path()
{
	if (auto path = flatpak_key(flatpak::section::instance, "app-path"))
		return std::filesystem::path(*path) / "OpenComposite";
	for (auto path: std::ranges::split_view(std::string_view(OPENCOMPOSITE_SEARCH_PATH), std::string_view(":")))
	{
		if (auto res = std::string_view(path); std::filesystem::exists(res))
			return res;
	}
	return {};
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
        pid(getpid())
{
	try
	{
		std::filesystem::path active_runtime_json = xdg_config_home() / "openxr/1/active_runtime.json";
		std::filesystem::create_directories(active_runtime_json.parent_path());
		move_file(active_runtime_json, backup_name(active_runtime_json));

		std::filesystem::create_symlink(manifest_path(), active_runtime_json);
		this->active_runtime_json = active_runtime_json;
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot set active OpenXR runtime: " << e.what() << std::endl;
	}

	try
	{
		auto opencomposite = opencomposite_path();
		if (not opencomposite.empty())
		{
			std::filesystem::path openvr_manifest = xdg_config_home() / "openvr/openvrpaths.vrpath";
			std::filesystem::create_directories(openvr_manifest.parent_path());
			move_file(openvr_manifest, backup_name(openvr_manifest));

			nlohmann::json manifest;
			manifest["runtime"] = {opencomposite.string()};
			manifest["version"] = 1;
			std::ofstream manifest_file(openvr_manifest.c_str());
			manifest_file.exceptions(std::ofstream::failbit);
			manifest_file << manifest;
			std::filesystem::permissions(openvr_manifest, std::filesystem::perms(0400), std::filesystem::perm_options::replace);
			this->openvr_manifest = openvr_manifest;
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot set active OpenXR runtime: " << e.what() << std::endl;
	}
}

active_runtime::~active_runtime()
{
	if (pid != getpid())
		return;
	try
	{
		if (not active_runtime_json.empty())
		{
			std::filesystem::remove(active_runtime_json);
			move_file(backup_name(active_runtime_json), active_runtime_json);
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot unset active OpenXR runtime: " << e.what() << std::endl;
	}

	try
	{
		if (not openvr_manifest.empty())
		{
			std::filesystem::remove(openvr_manifest);
			move_file(backup_name(openvr_manifest), openvr_manifest);
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot unset active OpenVR runtime: " << e.what() << std::endl;
	}
}
} // namespace wivrn
