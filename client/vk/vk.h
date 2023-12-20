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

#include "magic_enum.hpp"
#include "utils/check.h"
#include <string>
#include <vulkan/vulkan.h>

#define string_VkFormat(x) std::string(magic_enum::enum_name((VkFormat)(x)))
#define string_VkResult(x) std::string(magic_enum::enum_name((VkResult)(x)))

template <>
struct magic_enum::customize::enum_range<VkFormat>
{
	static constexpr int min = 0;
	static constexpr int max = 200;
};

template <typename... Args>
struct vk_chain
{
	std::tuple<Args...> structs;
};
