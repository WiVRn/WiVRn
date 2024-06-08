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
#include "utils/files.h"
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>

asset::asset(const std::filesystem::path & path)
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

static std::filesystem::path get_exe_path()
{
	// Linux only: see https://stackoverflow.com/a/1024937
	return std::filesystem::read_symlink("/proc/self/exe");
}

static std::filesystem::path get_asset_root()
{
	const char * path = std::getenv("WIVRN_ASSET_ROOT");
	if (path && strcmp(path, ""))
		return path;

	return get_exe_path().parent_path().parent_path() / "share" / "wivrn" / "assets";
}

static std::filesystem::path get_locale_root()
{
	const char * path = std::getenv("WIVRN_LOCALE_ROOT");
	if (path && strcmp(path, ""))
		return path;

	return get_exe_path().parent_path().parent_path() / "share" / "locale";
}

std::filesystem::path asset::asset_root()
{
	static std::filesystem::path root = get_asset_root();
	return root;
}

std::filesystem::path asset::locale_root()
{
	static std::filesystem::path root = get_locale_root();
	return root;
}

asset::asset(const std::filesystem::path & path)
{
	assert(path.is_relative());

	// TODO mmap
	// TODO load only once if it is already loaded

	spdlog::debug("Loading file asset {}", path.string());

	if (path.native().starts_with("locale/"))
	{
		bytes = utils::read_whole_file<std::byte>(locale_root() / path.native().substr(7));
	}
	else
	{
		bytes = utils::read_whole_file<std::byte>(asset_root() / path);
	}
}

#endif
