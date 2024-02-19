/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include <iostream>

active_runtime::active_runtime() : pid(getpid()), active_runtime_json(xdg_config_home() / "openxr" / "1" / "active_runtime.json")
{
	try
	{
		if (std::filesystem::exists(active_runtime_json))
			return;

		std::filesystem::create_directories(active_runtime_json.parent_path());

		auto exe = std::filesystem::read_symlink("/proc/self/exe");
		auto openxr_wivrn_json = exe.parent_path().parent_path() / "openxr_wivrn-dev.json";
		if (!std::filesystem::exists(openxr_wivrn_json))
			openxr_wivrn_json = std::filesystem::path(WIVRN_INSTALL_PREFIX) / "share" / "openxr" / "1" / "openxr_wivrn.json";

		std::filesystem::create_symlink(openxr_wivrn_json, active_runtime_json);

		to_be_deleted = true;
	}
	catch(std::exception& e)
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
	catch(std::exception& e)
	{
		std::cerr << "Cannot unset active OpenXR runtime: " << e.what() << std::endl;
	}
}
