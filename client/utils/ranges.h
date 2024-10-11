/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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
#include <iterator>
#include <type_traits>
#include <utility>

namespace utils
{

template <typename T>
concept has_const_iterator = requires {
	std::declval<typename T::const_iterator>();
};

template <typename T, bool>
struct get_const_iterator_aux;

template <typename T>
struct get_const_iterator_aux<T, true>
{
	using const_iterator = T::const_iterator;
};

template <typename T>
struct get_const_iterator_aux<T, false>
{
	using const_iterator = T::iterator;
};

template <typename T>
using get_const_iterator = get_const_iterator_aux<T, has_const_iterator<T>>::const_iterator;

template <typename T>
struct enumerate_range
{
	struct iterator
	{
		using underlying_iterator = std::conditional_t<std::is_const_v<T>, get_const_iterator<T>, typename T::iterator>;
		using underlying_value_type = std::conditional_t<std::is_const_v<T>, const typename T::value_type, typename T::value_type>;

		size_t index;
		underlying_iterator it;

		std::pair<size_t, underlying_value_type &> operator*() const
		{
			underlying_value_type & x = *it;
			return {index, x};
		}

		iterator & operator++()
		{
			++it;
			++index;
			return *this;
		}

		bool operator==(const iterator & other) const
		{
			return it == other.it;
		}

		bool operator!=(const iterator & other) const
		{
			return it != other.it;
		}
	};

	T & range;

	iterator begin() const
	{
		if constexpr (std::is_const_v<T>)
			return iterator{0, std::cbegin(range)};
		else
			return iterator{0, std::begin(range)};
	}

	iterator end() const
	{
		if constexpr (std::is_const_v<T>)
			return iterator{0, std::cend(range)};
		else
			return iterator{0, std::end(range)};
	}
};

template <typename T>
enumerate_range<const T> enumerate(const T & t)
{
	return {t};
}

template <typename T>
enumerate_range<T> enumerate(T & t)
{
	return {t};
}

} // namespace utils
