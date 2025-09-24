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

#include <cstddef>
#include <filesystem>
#include <span>

#ifdef __ANDROID__
struct AAsset;
#endif

namespace utils
{
class mapped_file
{
#ifdef __ANDROID__
	AAsset * android_asset = nullptr;
#endif

	std::span<const std::byte> bytes{};

	void map(int fd);
	void open(const std::filesystem::path & path);

public:
	mapped_file() = default;
	mapped_file(const mapped_file &) = delete;
	mapped_file(mapped_file &&);
	mapped_file & operator=(const mapped_file &) = delete;
	mapped_file & operator=(mapped_file &&);
	~mapped_file();

	mapped_file(int fd); // Does not take ownership of the file descriptor
	mapped_file(const std::filesystem::path & path);

	operator std::span<const std::byte>() const
	{
		return bytes;
	}

	operator std::string_view() const
	{
		return {reinterpret_cast<const char *>(bytes.data()), bytes.size_bytes()};
	}

	const std::byte * data() const
	{
		return bytes.data();
	}

	size_t size() const
	{
		return bytes.size();
	}
};
} // namespace utils
