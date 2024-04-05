/*
 * WiVRn VR streaming
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

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace utils
{

// single writer/single reader rung buffer
template <typename T, size_t capacity>
class ring_buffer
{
	std::array<T, capacity> container;
	// last position read
	std::atomic<size_t> read_index = 0;
	// last position written
	std::atomic<size_t> write_index = 0;

public:
	bool write(T && t)
	{
		size_t next_write = (write_index + 1) % capacity;
		if (next_write == read_index)
			return false;
		container[next_write] = std::move(t);
		write_index.store(next_write);
		return true;
	}

	std::optional<T> read()
	{
		if (read_index == write_index)
			return {};
		size_t next_read = (read_index + 1) % capacity;
		T res = std::move(container[next_read]);
		read_index.store(next_read);
		return std::move(res);
	}

	size_t size() const
	{
		size_t w = write_index;
		size_t r = read_index;
		return std::min(w - r, capacity + w - r);
	}
};

} // namespace utils
