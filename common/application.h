/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wivrn
{

struct application
{
	// localised names, with empty locale for default
	std::unordered_map<std::string, std::string> name;
	std::string exec;
	std::vector<std::byte> image; // In PNG
	std::optional<std::string> path;
};

std::unordered_map<std::string, application> list_applications(bool include_steam = true, bool load_icons = true);
} // namespace wivrn
