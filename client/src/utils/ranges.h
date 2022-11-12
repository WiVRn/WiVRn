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
#include <type_traits>
#include <utility>

namespace utils
{
template <typename T>
struct enumerate_range
{
	struct iterator
	{
		using underlying_iterator = std::conditional_t<std::is_const_v<T>, typename T::const_iterator, typename T::iterator>;
		using underlying_value_type = std::conditional_t<std::is_const_v<T>, const typename T::value_type, typename T::value_type>;

		size_t index;
		underlying_iterator it;

		std::pair<size_t, underlying_value_type &> operator*() const
		{
			return {index, *it};
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

template <typename T1, typename T2>
struct zip_range
{
	struct iterator
	{
		using iterator1 = decltype(std::begin(std::declval<T1 &>()));
		using iterator2 = decltype(std::begin(std::declval<T2 &>()));

		using value1 = decltype(*std::declval<iterator1>());
		using value2 = decltype(*std::declval<iterator2>());

		iterator1 iter1;
		iterator2 iter2;

		std::pair<value1 &, value2 &> operator*() const
		{
			return {*iter1, *iter2};
		}

		iterator & operator++()
		{
			++iter1;
			++iter2;
			return *this;
		}

		bool operator==(const iterator & other) const
		{
			return iter1 == other.iter1 || iter2 == other.iter2;
		}

		bool operator!=(const iterator & other) const
		{
			return !(*this == other);
		}
	};

	T1 & range1;
	T2 & range2;

	iterator begin() const
	{
		return iterator{std::begin(range1), std::begin(range2)};
	}

	iterator end() const
	{
		return iterator{std::end(range1), std::end(range2)};
	}
};

template <typename T1, typename T2>
zip_range<T1, T2> zip(T1 & range1, T2 & range2)
{
	return zip_range<T1, T2>{range1, range2};
}
} // namespace utils
