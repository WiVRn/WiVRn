/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <boost/pfr.hpp>
#include <concepts>
#include <fstream>
#include <string>
#include <type_traits>

#if __has_include(<boost/pfr/core_name.hpp>)
#define BOOST_HAS_CORE_NAME
#endif

namespace details
{
template <class>
struct is_stdarray : public std::false_type
{};

template <class T, size_t N>
struct is_stdarray<std::array<T, N>> : public std::true_type
{};

template <typename T>
inline constexpr bool is_stdarray_v = is_stdarray<T>::value;

template <typename T, typename Enable = void>
struct csv_logger_traits;

template <typename T>
struct csv_logger_traits<T, std::enable_if_t<std::is_aggregate_v<T> && !is_stdarray_v<T>>>
{
#ifdef BOOST_HAS_CORE_NAME
	static void write_header(std::ostream & out, const std::string & name, bool first = true)
	{
		boost::pfr::for_each_field_with_name(T{}, [&](std::string_view member_name, const auto & value) {
			csv_logger_traits<std::remove_cvref_t<decltype(value)>>::write_header(out, name + "." + (std::string)member_name, first);
			first = false;
		});
	}
#endif

	static void write_line(std::ostream & out, const T & value, bool first = true)
	{
		boost::pfr::for_each_field(value, [&](const auto & member) {
			csv_logger_traits<std::remove_cvref_t<decltype(member)>>::write_line(out, member, first);
			first = false;
		});
	}
};

template <typename T>
struct csv_logger_traits<T, std::enable_if_t<is_stdarray_v<T>>>
{
	static void write_header(std::ostream & out, const std::string & name, bool first = true)
	{
		for (int i = 0; i < std::tuple_size_v<T>; i++)
		{
			csv_logger_traits<std::tuple_element_t<0, T>>::write_header(out, name + "[" + std::to_string(i) + "]", first and i == 0);
		}
	}

	static void write_line(std::ostream & out, const T & value, bool first = true)
	{
		for (int i = 0; i < std::tuple_size_v<T>; i++)
		{
			csv_logger_traits<std::tuple_element_t<0, T>>::write_line(out, value[i], first and i == 0);
		}
	}
};

template <typename T>
struct csv_logger_traits<T, std::enable_if_t<!std::is_aggregate_v<T>>>
{
	static void write_header(std::ostream & out, std::string_view name, bool first = true)
	{
		if (not first)
			out << ",";
		out << name.substr(1);
	}

	static void write_line(std::ostream & out, const T & value, bool first = true)
	{
		if (not first)
			out << ",";
		out << value;
	}
};

} // namespace details

template <typename T, typename Stream = std::ofstream>
        requires std::derived_from<Stream, std::ostream>
class csv_logger
{
	Stream stream;

public:
	template <typename... Args>
	csv_logger(Args &&... args) : stream(std::forward<Args>(args)...)
	{
#ifdef BOOST_HAS_CORE_NAME
		details::csv_logger_traits<T>::write_header(stream, "");
		stream << std::endl;
#endif
	}

	void write(const T & data)
	{
		details::csv_logger_traits<T>::write_line(stream, data);
		stream << std::endl;
	}
};
