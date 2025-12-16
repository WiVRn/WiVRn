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
#include "driver/configuration.h"
#include "utils/flatpak.h"
#include "utils/overloaded.h"
#include "utils/xdg_base_directory.h"
#include "wivrn_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

static std::vector<std::filesystem::path> filter_files(const std::filesystem::path & directory, const std::string prefix)
{
	std::vector<std::filesystem::path> result;
	for (const auto & file: std::filesystem::directory_iterator{directory})
	{
		if (file.path().filename().native().starts_with(prefix))
			result.push_back(file.path());
	}
	return result;
}

namespace wivrn
{

std::vector<std::filesystem::path> active_runtime::manifest_path()
{
	const std::filesystem::path location = "share/openxr/1";
	const std::filesystem::path prefix = "openxr_wivrn";
	// Check if in a flatpak
	if (auto path = flatpak_key(flatpak::section::instance, "app-path"))
		return filter_files(*path / location, prefix);

	// Check if running from build directory
	auto exe = std::filesystem::read_symlink("/proc/self/exe");
	auto dev_manifest = exe.parent_path().parent_path() / "openxr_wivrn-dev.json";
	if (std::filesystem::exists(dev_manifest))
		return {dev_manifest};

	// Assume we are installed
	return filter_files(WIVRN_INSTALL_PREFIX / location, prefix);
}

std::filesystem::path active_runtime::openvr_compat_path()
{
	return std::visit(
	        utils::overloaded{
	                [](std::monostate) {
		                // no user configuration: return default
		                std::optional<std::filesystem::path> flatpak_root = flatpak_key(flatpak::section::instance, "app-path");
		                // flatpak default
		                if (flatpak_root)
			                return *flatpak_root / "xrizer";

		                for (auto path: std::ranges::split_view(std::string_view(OVR_COMPAT_SEARCH_PATH), std::string_view(":")))
		                {
			                if (std::filesystem::path res = std::string_view(path); std::filesystem::exists(res))
				                return res;
		                }
		                return std::filesystem::path();
	                },
	                [](const std::string & path) {
		                // exlicit value, use it
		                std::optional<std::filesystem::path> flatpak_root = flatpak_key(flatpak::section::instance, "app-path");
		                return flatpak_root.value_or("") / path;
	                },
	                [](std::nullptr_t) {
		                // explicit null, don't set any compat path
		                return std::filesystem::path();
	                },
	        },
	        configuration().openvr_compat_path);
}

static std::filesystem::path backup_name(std::filesystem::path file)
{
	file += ".wivrn-backup";
	return file;
}

static void move_file(const std::filesystem::path & from, const std::filesystem::path & to)
{
	auto from_status = std::filesystem::symlink_status(from);
	if (from_status.type() == std::filesystem::file_type::not_found)
		return;
	std::filesystem::rename(from, to);
}

static const std::filesystem::path & backup_and_symlink(
        const std::filesystem::path & location,
        const std::filesystem::path & dst)
{
	std::filesystem::create_directories(location.parent_path());
	std::error_code ec;
	if (std::filesystem::equivalent(location, dst, ec))
		return location;
	// The file may be a dead symlink
	if (ec)
		std::filesystem::remove(location);

	move_file(location, backup_name(location));

	std::filesystem::create_symlink(dst, location);
	return location;
}

static std::string get_abi(const std::filesystem::path & filename)
{
	std::string abi = filename.stem().extension();
	// https://registry.khronos.org/OpenXR/specs/1.0/loader.html#architecture-identifiers
	if (not abi.empty() and
	    std::ranges::contains(std::array{
	                                  "x32",
	                                  "x86_64",
	                                  "i686",
	                                  "aarch64",
	                                  "armv7a-vfp",
	                                  "armv5te",
	                                  "mips64",
	                                  "mips",
	                                  "ppc64",
	                                  "ppc64el",
	                                  "s390x",
	                                  "hppa",
	                                  "alpha",
	                                  "ia64",
	                                  "m68k",
	                                  "m68k",
	                                  "riscv64",
	                                  "sparc64",
	                          },
	                          abi.substr(1)))
		return abi;
	return "";
}

active_runtime::active_runtime() :
        pid(getpid())
{
	for (const auto & manifest: manifest_path())
	{
		try
		{
			active_runtime_json.push_back(backup_and_symlink(
			        xdg_config_home() / ("openxr/1/active_runtime" + get_abi(manifest) + ".json"),
			        manifest));
		}
		catch (std::exception & e)
		{
			std::cerr << "Cannot set active OpenXR runtime: " << e.what() << std::endl;
		}
	}

	try
	{
		auto ovr_compat = openvr_compat_path();
		if (not ovr_compat.empty())
		{
			std::filesystem::path openvr_manifest = xdg_config_home() / "openvr/openvrpaths.vrpath";
			std::filesystem::create_directories(openvr_manifest.parent_path());
			move_file(openvr_manifest, backup_name(openvr_manifest));

			nlohmann::json manifest;
			manifest["runtime"] = {ovr_compat.string()};
			manifest["version"] = 1;
			std::ofstream manifest_file(openvr_manifest.c_str());
			manifest_file.exceptions(std::ofstream::failbit);
			manifest_file << manifest;
			this->openvr_manifest = openvr_manifest;
		}
	}
	catch (std::exception & e)
	{
		std::cerr << "Cannot set active OpenVR runtime: " << e.what() << std::endl;
	}
}

active_runtime::~active_runtime()
{
	if (pid != getpid())
		return;
	for (const auto & manifest: active_runtime_json)
	{
		try
		{
			std::filesystem::remove(manifest);
			move_file(backup_name(manifest), manifest);
		}
		catch (std::exception & e)
		{
			std::cerr << "Cannot unset active OpenXR runtime " << manifest << ": " << e.what() << std::endl;
		}
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
