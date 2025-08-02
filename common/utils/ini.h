/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <charconv>
#include <istream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace wivrn
{
class ini
{
public:
	struct key_value
	{
		std::string_view section;
		std::string_view key;
		std::string_view locale;
		std::string_view value;
	};

private:
	std::string contents;
	std::vector<key_value> lines;

public:
	ini() = default;
	ini(ini &&) = default;
	ini(const ini &) = delete;
	ini & operator=(ini &&) = default;
	ini & operator=(const ini &) = delete;

	ini(std::istream & file);

	std::optional<std::string_view> get_optional(std::string_view section, std::string_view key) const;

	template <typename T>
	T get(std::string_view section, std::string_view key, const T & default_value = T{}) const
	{
		using return_type = std::decay_t<T>;

		auto value = get_optional(section, key);

		if (not value)
			return default_value;

		if constexpr (std::is_same_v<return_type, std::string_view>)
			return *value;

		if constexpr (std::is_same_v<return_type, std::string>)
			return std::string{*value};

		if constexpr (std::is_integral_v<return_type>)
		{
			return_type v;
			if (std::from_chars(value->data(), value->data() + value->size(), v).ec != std::errc{})
				return default_value;

			return v;
		}
	}
};
} // namespace wivrn
