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

#include "utils/strings.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#ifdef __ANDROID__
#include "android/property.h"
#endif

template <typename T>
std::optional<T> env(std::string_view name);

template <>
inline std::optional<std::string> env(std::string_view name)
{
#ifdef __ANDROID__
	auto android_sysprop_name = std::format("debug.wivrn.{}", name);
	return get_property(android_sysprop_name.c_str());
#else
	std::string env_name = "WIVRN_";
	env_name.reserve(env_name.size() + name.size());
	for (const auto & c: name)
		env_name += std::toupper(c);
	const char * value = std::getenv(env_name.c_str());
	if (value)
		return value;
	return std::nullopt;
#endif
}

template <>
inline std::optional<uint32_t> env(std::string_view name)
{
	const auto value = env<std::string>(name);
	if (not value)
		return std::nullopt;
	try
	{
		const unsigned long parsed = std::stoul(*value);
		if (parsed > std::numeric_limits<uint32_t>::max())
			return std::nullopt;
		return static_cast<uint32_t>(parsed);
	}
	catch (std::runtime_error & e)
	{
		spdlog::warn("failed to parse {} (value: {}: {}", name, *value, e.what());
		return std::nullopt;
	}
}

template <>
inline std::optional<bool> env(std::string_view name)
{
	const auto value = env<std::string>(name);
	if (not value)
		return std::nullopt;
	if (strcmp(value->c_str(), "1") == 0 || strcasecmp(value->c_str(), "true") == 0 || strcasecmp(value->c_str(), "yes") == 0 || strcasecmp(value->c_str(), "on") == 0)
		return true;
	if (strcmp(value->c_str(), "0") == 0 || strcasecmp(value->c_str(), "false") == 0 || strcasecmp(value->c_str(), "no") == 0 || strcasecmp(value->c_str(), "off") == 0)
		return false;
	return std::nullopt;
}

template <>
inline std::optional<std::unordered_map<std::string, std::string>> env(std::string_view name)
{
	const auto values = env<std::string>(name);
	if (not values)
		return std::nullopt;

	std::unordered_map<std::string, std::string> map;
	for (auto value: utils::split(*values, ","))
	{
		if (auto pos = value.find('='); pos != std::string::npos)
			map.insert(std::make_pair(value.substr(0, pos), value.substr(pos + 1)));
	}

	return map;
}

template <>
inline std::optional<std::unordered_set<std::string>> env(std::string_view name)
{
	const auto values = env<std::string>(name);
	if (not values)
		return std::nullopt;

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
	return std::unordered_set<std::string>{std::from_range, utils::split(*values, ",")};
#else
	std::optional<std::unordered_set<std::string>> res{std::in_place};
	for (auto & val: utils::split(*values))
		res->insert(std::move(val));
	return res;
#endif
}