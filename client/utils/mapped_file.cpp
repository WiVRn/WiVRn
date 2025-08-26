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
#include <asm-generic/mman-common.h>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>

utils::mapped_file::~mapped_file()
{
	if (fd >= 0)
	{
		::munmap(bytes.data(), bytes.size());
		::close(fd);
	}
}

utils::mapped_file::mapped_file(mapped_file && other) :
        fd(other.fd),
        bytes(other.bytes)
{
	other.fd = -1;
	other.bytes = {};
}

utils::mapped_file & utils::mapped_file::operator=(mapped_file && other)
{
	std::swap(fd, other.fd);
	std::swap(bytes, other.bytes);

	return *this;
}

utils::mapped_file::mapped_file(const std::filesystem::path & path) :
        mapped_file(::open(path.c_str(), O_RDONLY))
{
}

utils::mapped_file::mapped_file(int fd) :
        fd(fd)
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
