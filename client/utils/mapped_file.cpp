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

#include "mapped_file.h"
#include "application.h"
#include <asm-generic/mman-common.h>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

namespace
{
#ifndef __ANDROID__
std::filesystem::path get_exe_path()
{
	// Linux only: see https://stackoverflow.com/a/1024937
	return std::filesystem::read_symlink("/proc/self/exe");
}

std::filesystem::path asset_root()
{
	static std::filesystem::path root = []() -> std::filesystem::path {
		const char * path = std::getenv("WIVRN_ASSET_ROOT");
		if (path && strcmp(path, ""))
			return path;

		return get_exe_path().parent_path().parent_path() / "share" / "wivrn" / "assets";
	}();

	return root;
}

std::filesystem::path locale_root()
{
	static std::filesystem::path root = []() -> std::filesystem::path {
		const char * path = std::getenv("WIVRN_LOCALE_ROOT");
		if (path && strcmp(path, ""))
			return path;

		return get_exe_path().parent_path().parent_path() / "share" / "locale";
	}();

	return root;
}
#endif
} // namespace

utils::mapped_file::~mapped_file()
{
#ifdef __ANDROID__
	if (android_asset)
		AAsset_close(android_asset);
	else
		::munmap(const_cast<std::byte *>(bytes.data()), bytes.size());
#else
	::munmap(const_cast<std::byte *>(bytes.data()), bytes.size());
#endif
}

utils::mapped_file::mapped_file(mapped_file && other) :
#ifdef __ANDROID__
        android_asset(other.android_asset),
#endif
        bytes(other.bytes)
{
#ifdef __ANDROID__
	other.android_asset = nullptr;
#endif
	other.bytes = {};
}

utils::mapped_file & utils::mapped_file::operator=(mapped_file && other)
{
#ifdef __ANDROID__
	std::swap(android_asset, other.android_asset);
#endif
	std::swap(bytes, other.bytes);

	return *this;
}

void utils::mapped_file::map(int fd)
{
	off_t size = lseek(fd, 0, SEEK_END);
	void * addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 /* offset */);

	if (addr == MAP_FAILED)
	{
		char path[2000];
		size_t pathsize = ::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), path, sizeof(path));

		throw std::system_error(errno, std::system_category(), "mmap " + std::string{path, pathsize});
	}

	bytes = {reinterpret_cast<std::byte *>(addr), (size_t)size};
}

void utils::mapped_file::open(const std::filesystem::path & path)
{
	int fd = ::open(path.native().c_str(), O_RDONLY);

	if (fd < 0)
		throw std::system_error(errno, std::system_category(), "open " + std::string{path});

	map(fd);
	::close(fd);
}

utils::mapped_file::mapped_file(const std::filesystem::path & path)
{
	if (path.native().starts_with("assets://"))
	{
		std::filesystem::path asset_path = path.native().substr(9);

#ifdef __ANDROID__
		android_asset = AAssetManager_open(application::asset_manager(), asset_path.c_str(), AASSET_MODE_BUFFER);

		if (!android_asset)
			throw std::runtime_error("Cannot open Android asset " + path.string());

		bytes = {reinterpret_cast<const std::byte *>(AAsset_getBuffer(android_asset)), (size_t)AAsset_getLength64(android_asset)};
#else
		if (asset_path.native().starts_with("locale/"))
			open(locale_root() / asset_path.native().substr(7));
		else
			open(asset_root() / asset_path.native());
#endif
	}
	else
		open(path);
}

utils::mapped_file::mapped_file(int fd)
{
	map(fd);
}
