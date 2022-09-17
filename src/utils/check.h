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

#include <stdexcept>
#include <system_error>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#if __has_include(<vulkan/vk_enum_string_helper.h>)
#include <vulkan/vk_enum_string_helper.h>
#else
#include "external/magic_enum.hpp"
#define string_VkResult(x) std::string(magic_enum::enum_name((VkResult)(x)))
#endif

namespace xr
{
const std::error_category & error_category();
}

namespace vk
{
const std::error_category & error_category();
}

static inline VkResult check(VkResult result, const char * statement)
{
	if (result != VK_SUCCESS)
		throw std::system_error(result, vk::error_category(), statement);

	return result;
}

static inline VkResult check(VkResult result, const char * /*statement*/, const char * message)
{
	if (result != VK_SUCCESS)
		throw std::system_error(result, vk::error_category(), message);

	return result;
}

static inline XrResult check(XrResult result, const char * statement)
{
	if (!XR_SUCCEEDED(result))
		throw std::system_error(result, xr::error_category(), statement);

	return result;
}

static inline XrResult check(XrResult result, const char * /*statement*/, const char * message)
{
	if (!XR_SUCCEEDED(result))
		throw std::system_error(result, xr::error_category(), message);

	return result;
}

#define CHECK_VK(result, ...) check(result, #result __VA_OPT__(, ) __VA_ARGS__)
#define CHECK_XR(result, ...) check(result, #result __VA_OPT__(, ) __VA_ARGS__)
