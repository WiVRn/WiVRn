/*
 * WiVRn VR streaming
 * Copyright (C) 2023-2025 Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include <fstream>
#include <iterator>
#include <type_traits>
#include <vector>

namespace utils
{
template <typename T>
static std::conditional_t<std::is_same_v<T, std::string>, std::string, std::vector<T>> read_whole_file(std::filesystem::path filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	file.exceptions(std::ios_base::badbit | std::ios_base::failbit);

	size_t size = file.tellg();
	file.seekg(0);

	if constexpr (std::is_same_v<T, std::string>)
	{
		std::string string;
		string.resize(size);

		file.read(reinterpret_cast<char *>(string.data()), size);

		return string;
	}
	else
	{
		if constexpr (sizeof(T) > 1)
			size = size - size % sizeof(T);

		std::vector<T> bytes(size / sizeof(T));

		file.read(reinterpret_cast<char *>(bytes.data()), size);

		return bytes;
	}
}

template <typename T>
        requires std::contiguous_iterator<typename T::iterator>
static void write_whole_file(std::filesystem::path filename, T bytes)
{
	std::ofstream file(filename, std::ios::binary);
	file.exceptions(std::ios_base::badbit | std::ios_base::failbit);

	file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size() * sizeof(typename T::value_type));
}
} // namespace utils
