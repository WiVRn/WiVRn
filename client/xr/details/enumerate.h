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

#ifdef __ANDROID__
#include <jni.h>
#endif

#include "utils/typename.h"
#include "xr/check.h"
#include <type_traits>
#include <vector>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

namespace xr::details
{

template <typename T>
struct base_struct
{
	using type = T;
};
template <typename T>
using base_struct_t = base_struct<T>::type;

template <>
struct base_struct<XrSwapchainImageVulkanKHR>
{
	using type = XrSwapchainImageBaseHeader;
};

template <typename T>
struct structure_traits
{
	static constexpr inline XrStructureType type = XR_TYPE_UNKNOWN;
};

#define TRAIT(S, T)                                               \
	template <>                                               \
	struct structure_traits<S>                                \
	{                                                         \
		static constexpr inline XrStructureType type = T; \
	};

XR_LIST_STRUCTURE_TYPES(TRAIT)
#undef TRAIT

template <typename T>
constexpr inline XrStructureType structure_type = structure_traits<T>::type;

template <typename T, typename F, typename... Args>
void enumerate(F f, T & data, Args &&... args)
{
	using V = T::value_type;
	uint32_t count;
	uint32_t capacity = data.size();
	// For strings, add the null byte as capacity
	if (std::is_same_v<T, std::string> and capacity)
		capacity++;
	XrResult result = f(std::forward<Args>(args)..., capacity, &count, reinterpret_cast<base_struct_t<V> *>(data.data()));

	if (result == XR_ERROR_SIZE_INSUFFICIENT or (data.empty() and XR_SUCCEEDED(result) and count > 0))
	{
		if constexpr (structure_type<V> != XR_TYPE_UNKNOWN)
			data.resize(count, V{.type = structure_type<V>});
		else if constexpr (std::is_same_v<T, std::string>)
			data.resize(count - 1); // count includes the null terminator
		else
			data.resize(count);
		return enumerate<T>(f, data, std::forward<Args>(args)...);
	}

	if (not XR_SUCCEEDED(result))
		throw std::system_error(result, xr::error_category(), "enumerating " + type_name<T>());

	data.resize(count - std::is_same_v<T, std::string>);
}

template <typename T, typename F, typename... Args>
auto enumerate(F f, Args &&... args) -> auto
{
	using array_type = std::conditional_t<std::is_same_v<T, char>, std::string, std::vector<T>>;
	array_type array;
	enumerate(f, array, std::forward<Args>(args)...);
	return array;
}

template <size_t i, typename... T>
void resize_and_link(std::tuple<std::vector<T>...> & tup, size_t size)
{
	auto & vec = std::get<i>(tup);
	vec.resize(size);
	for (size_t j = 0; j < size; ++j)
	{
		auto & item = vec[j];
		item.type = structure_type<std::tuple_element_t<i, std::tuple<T...>>>;
		if constexpr (i < sizeof...(T) - 1)
		{
			item.next = &std::get<i + 1>(tup)[j];
		}
	}
	if constexpr (i > 0)
	{
		resize_and_link<i - 1>(tup, size);
	}
}

template <size_t i = 0, typename... T>
void resize(std::tuple<std::vector<T>...> & tup, size_t size)
{
	if constexpr (i < sizeof...(T))
	{
		auto & vec = std::get<i>(tup);
		vec.resize(size);
		resize<i + 1>(tup, size);
	}
}

template <typename... T, typename F, typename... Args>
void enumerate2(F f, std::tuple<std::vector<T>...> & data, Args &&... args)
{
	using T0 = std::tuple_element_t<0, std::tuple<T...>>;
	uint32_t count;
	auto & data0 = std::get<0>(data);
	uint32_t capacity = data0.size();
	XrResult result = f(std::forward<Args>(args)..., capacity, &count, reinterpret_cast<base_struct_t<T0> *>(data0.data()));

	if (result == XR_ERROR_SIZE_INSUFFICIENT or (capacity == 0 and XR_SUCCEEDED(result) and count > 0))
	{
		resize_and_link<sizeof...(T) - 1>(data, count);
		return enumerate2<T...>(f, data, std::forward<Args>(args)...);
	}

	if (not XR_SUCCEEDED(result))
		throw std::system_error(result, xr::error_category(), "enumerating " + type_name<T0>());

	resize(data, count);
}

template <typename... T, typename F, typename... Args>
auto enumerate2(F f, Args &&... args) -> auto
{
	using array_type = std::tuple<std::vector<T>...>;
	array_type array;
	enumerate2(f, array, std::forward<Args>(args)...);
	return array;
}

} // namespace xr::details
