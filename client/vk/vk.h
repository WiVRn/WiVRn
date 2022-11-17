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

#include "external/magic_enum.hpp"
#include "utils/check.h"
#include "vk/buffer.h"
#include "vk/buffer_view.h"
#include "vk/command_pool.h"
#include "vk/device_memory.h"
#include "vk/image.h"
#include "vk/pipeline.h"
#include "vk/renderpass.h"
#include "vk/shader.h"
#include <string>
#include <vulkan/vulkan.h>

#if __has_include(<vulkan/vk_enum_string_helper.h>)
#include <vulkan/vk_enum_string_helper.h>
#else
#define string_VkFormat(x) std::string(magic_enum::enum_name((VkFormat)(x)))
#endif

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
