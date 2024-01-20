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

#include <spdlog/fmt/fmt.h>
#include <glm/fwd.hpp>


// /usr/include/fmt/chrono.h:2017

template<glm::length_t L, typename T, glm::qualifier Q, typename Char>
struct fmt::formatter<glm::vec<L, T, Q>, Char> //: fmt::formatter<T>
{
	// auto parse(basic_format_parse_context<Char>& ctx) const -> decltype(ctx.begin())
	// {
	// 	auto begin = ctx.begin(), end = ctx.end();
	// 	// if (begin != end && *begin == 'L') {
	// 		// ++begin;
	// 		// localized = true;
	// 	// }
	// 	return begin;
	// }

	auto parse(basic_format_parse_context<Char>& ctx) const // -> decltype(ctx.begin())
	{
		return ctx.begin();
	}


	template <typename FormatContext>
	auto format(const glm::vec<L, T, Q>& v, FormatContext& ctx) const -> decltype(ctx.out())
	{
		if constexpr (L == 1)
		{
			return fmt::format_to(ctx.out(), "[ {} ]", v.x);
		}

		if constexpr (L == 2)
		{
			return fmt::format_to(ctx.out(), "[ {}, {} ]", v.x, v.y);
		}

		if constexpr (L == 3)
		{
			return fmt::format_to(ctx.out(), "[ {}, {}, {} ]", v.x, v.y, v.z);
		}

		if constexpr (L == 4)
		{
			return fmt::format_to(ctx.out(), "[ {}, {}, {}, {} ]", v.x, v.y, v.z, v.w);
		}



		// return formatter<std::string_view>(


		// auto time = std::tm();
		// time.tm_wday = static_cast<int>(wd.c_encoding());
		// detail::get_locale loc(localized, ctx.locale());
		// auto w = detail::tm_writer<decltype(ctx.out()), Char>(loc, ctx.out(), time);
		// w.on_abbr_weekday();
		// return w.out();
	}

};


template<typename T, glm::qualifier Q, typename Char>
struct fmt::formatter<glm::qua<T, Q>, Char>
{

	auto parse(basic_format_parse_context<Char>& ctx) const // -> decltype(ctx.begin())
	{
		return ctx.begin();
	}

	template <typename FormatContext>
	auto format(const glm::qua<T, Q>& q, FormatContext& ctx) const -> decltype(ctx.out())
	{
		return fmt::format_to(ctx.out(), "[ {}, {}, {}, {} ]", q.w, q.x, q.y, q.z);
	}

};

/*
template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct fmt::formatter<glm::mat<C, R, T, Q>> : fmt::formatter<std::string>
{
};



template<typename T, glm::qualifier Q>
struct fmt::formatter<glm::qua<T, Q>> : fmt::formatter<std::string>
{
};*/
