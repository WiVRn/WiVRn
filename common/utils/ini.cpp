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

#include "ini.h"
#include <ranges>

static constexpr std::string_view strip(std::string_view sv)
{
	size_t prefix_size = sv.find_first_not_of(' ');

	if (prefix_size == std::string_view::npos)
		return {};

	sv.remove_prefix(prefix_size);

	size_t suffix_size = sv.find_last_not_of(' ');
	if (suffix_size == std::string_view::npos)
		return {};

	sv.remove_suffix(sv.size() - suffix_size - 1);

	return sv;
}

static constexpr std::optional<wivrn::ini::key_value> parse_key_value(std::string_view section, std::string_view line)
{
	size_t equal_sign = line.find('=');
	if (equal_sign == std::string_view::npos)
		return {};

	auto key = strip(line.substr(0, equal_sign)); // TODO locale
	auto value = strip(line.substr(equal_sign + 1));

	return wivrn::ini::key_value{
	        .section = section,
	        .key = key,
	        .locale = "",
	        .value = value,
	};
}

std::optional<std::string_view> wivrn::ini::get_optional(std::string_view section, std::string_view key) const
{
	for (const auto & line: lines)
	{
		if (line.section == section and line.key == key)
			return line.value;
	}

	return std::nullopt;
}

wivrn::ini::ini(std::istream & file)
{
	contents = {std::istreambuf_iterator<char>{file}, {}};

	std::string_view current_section;

	for (auto bounds: contents | std::views::split('\n'))
	{
		auto line = strip({bounds.begin(), bounds.end()});

		if (line.starts_with('#'))
			continue;

		if (line.starts_with('[') and line.ends_with(']'))
		{
			current_section = line.substr(1, line.size() - 2);
			continue;
		}

		auto kv = parse_key_value(current_section, line);

		if (kv)
			lines.push_back(*kv);
	}
}
