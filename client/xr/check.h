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

#include <openxr/openxr.h>
#include <system_error>

namespace xr
{
const std::error_category & error_category();
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

#define CHECK_XR(result, ...) check(result, #result __VA_OPT__(, ) __VA_ARGS__)
