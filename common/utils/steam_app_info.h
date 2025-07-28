/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include <unordered_map>
#include <variant>
#include <vector>

namespace wivrn
{
class steam_app_info
{
public:
	using info = std::unordered_map<std::string, std::variant<uint32_t, std::string_view>>;

private:
	std::vector<char> data;

	std::unordered_map<int, info> app_data;

public:
	steam_app_info(std::filesystem::path path);
	steam_app_info() = default;
	steam_app_info(const steam_app_info &) = delete;
	steam_app_info(steam_app_info &&) = default;
	steam_app_info & operator=(const steam_app_info &) = delete;
	steam_app_info & operator=(steam_app_info &&) = default;

	const auto & get(int appid) const
	{
		return app_data.at(appid);
	}
};
} // namespace wivrn
