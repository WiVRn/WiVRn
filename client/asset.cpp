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

#include "asset.h"
#include "application.h"
#include <fstream>
#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>

asset::asset(std::filesystem::path path)
{
	spdlog::debug("Loading Android asset {}", path.string());
	android_asset = AAssetManager_open(application::asset_manager(), path.c_str(), AASSET_MODE_BUFFER);

	if (!android_asset)
		throw std::runtime_error("Cannot open Android asset " + path.string());

	bytes = std::span<const std::byte>{reinterpret_cast<const std::byte *>(AAsset_getBuffer(android_asset)), (size_t)AAsset_getLength64(android_asset)};
}

asset::asset(asset && other) :
	android_asset(other.android_asset)
{
	other.android_asset = nullptr;
}

asset & asset::operator=(asset && other)
{
	std::swap(android_asset, other.android_asset);
	return *this;
}

asset::~asset()
{
	if (android_asset)
		AAsset_close(android_asset);
}

#else

asset::asset(std::filesystem::path path)
{
	// TODO mmap
	// TODO: load only once if it is already loaded

	spdlog::debug("Loading file asset {}", path.string());

	if (path.is_relative())
		path = "assets" / path;

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	file.exceptions(std::ios_base::badbit | std::ios_base::failbit);

	size_t size = file.tellg();
	file.seekg(0);

	bytes.resize(size);

	file.read(reinterpret_cast<char *>(bytes.data()), size);
}

#endif
