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

#include "xr/check.h"
#include "xr/to_string.h"
#include <system_error>

namespace
{
struct : std::error_category
{
	const char * name() const noexcept override
	{
		return "openxr";
	}

	std::string message(int condition) const override
	{
		return xr::to_string(static_cast<XrResult>(condition));
	}
} openxr_error_category;
} // namespace

const std::error_category & xr::error_category()
{
	return openxr_error_category;
}
