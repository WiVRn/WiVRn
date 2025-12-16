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

#pragma once

#include <filesystem>
#include <unistd.h>
#include <vector>

namespace wivrn
{

class active_runtime
{
	std::vector<std::filesystem::path> active_runtime_json;
	std::filesystem::path openvr_manifest;
	pid_t pid;

public:
	active_runtime();
	active_runtime(const active_runtime &) = delete;
	active_runtime & operator=(const active_runtime &) = delete;
	~active_runtime();

	static std::vector<std::filesystem::path> manifest_path();
	static std::filesystem::path openvr_compat_path();
};
} // namespace wivrn
