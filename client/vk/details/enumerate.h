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

#include "utils/typename.h"
#include "vk/vk.h"
#include <system_error>
#include <vector>
#include <vulkan/vulkan.h>

namespace vk::details
{

template <typename T>
struct structure_traits_base
{
	static constexpr inline VkStructureType type = static_cast<VkStructureType>(-1);
	using base = T;
};

template <typename T>
struct structure_traits : structure_traits_base<T>
{};

template <typename T>
constexpr inline VkStructureType structure_type = structure_traits<T>::type;

template <typename T, typename F, typename... Args>
auto enumerate(F f, Args &&... args) -> auto
{
	std::vector<T> array;
	uint32_t count;
	VkResult result;

	result = f(std::forward<Args>(args)..., &count, nullptr);

	if (result >= 0)
	{
		if constexpr (structure_type<T> == static_cast<VkStructureType>(-1))
		{
			array.resize(count);
		}
		else
		{
			T default_value{};
			default_value.type = structure_type<T>;
			array.resize(count, default_value);
		}
		result = f(std::forward<Args>(args)..., &count, reinterpret_cast<typename structure_traits<T>::base *>(array.data()));
	}

	if (result < 0)
	{
		throw std::system_error(result, vk::error_category(), "enumerating " + type_name<T>());
	}

	return array;
}

} // namespace vk::details
