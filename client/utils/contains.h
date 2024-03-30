/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <algorithm>

namespace utils
{

template <typename Rng, typename T>
bool contains(Rng && range, const T & value)
{
	auto it = std::find(range.begin(), range.end(), value);

	return it != range.end();
}

// Check if all values of range2 are in range1
template <typename Rng1, typename Rng2>
bool contains_all(Rng1 && range1, Rng2 && range2)
{
	for (const auto & i: range2)
	{
		if (!contains(range1, i))
			return false;
	}

	return true;
}

} // namespace utils
