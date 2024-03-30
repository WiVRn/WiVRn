/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <cstdint>
#include <vector>

namespace xrt::drivers::wivrn
{

// Intended to be the last element of a serializable type
// contains the data referenced by spans
struct data_holder
{
	data_holder() = default;
	data_holder(const data_holder &) = delete;
	data_holder(data_holder &&) = default;
	data_holder & operator=(const data_holder &) = delete;
	data_holder & operator=(data_holder &&) = default;

	std::vector<uint8_t> c;
};

} // namespace xrt::drivers::wivrn
