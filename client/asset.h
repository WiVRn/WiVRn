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

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#ifdef __ANDROID__
struct AAsset;
#endif


class asset
{
#ifdef __ANDROID__
	AAsset * android_asset = nullptr;
	std::span<const std::byte> bytes;
#else
	static std::filesystem::path asset_root();
	std::vector<std::byte> bytes;
#endif

public:
	asset() = default;
	asset(const std::filesystem::path& path);

#ifdef __ANDROID__
	asset(asset && other);
	asset & operator=(asset && other);
	~asset();
#else
	asset(asset && other) = default;
	asset & operator=(asset && other) = default;
#endif

	asset(const asset &) = delete;
	asset & operator=(const asset &) = delete;

	const std::byte * data() const
	{
		return bytes.data();
	}

	size_t size() const
	{
		return bytes.size();
	}

	operator std::span<const std::byte>() const
	{
		return bytes;
	}

	operator std::string() const
	{
		return std::string(reinterpret_cast<const char*>(data()), size());
	}
};
