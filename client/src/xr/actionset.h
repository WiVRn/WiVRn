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

#include "utils/handle.h"
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

namespace xr
{
class instance;

class actionset : public utils::handle<XrActionSet>
{
	instance * inst;

public:
	actionset() = default;
	actionset(instance & inst, const std::string & name, const std::string & localized_name, uint32_t priority = 0);
	actionset(actionset &&) = default;
	actionset & operator=(actionset &&) = default;

	XrAction create_action(XrActionType type, const std::string & name, const std::string & localized_name, const std::vector<std::string> & subactionpaths = {});

	XrAction create_action(XrActionType type, const std::string & name, const std::vector<std::string> & subactionpaths = {})
	{
		return create_action(type, name, name, subactionpaths);
	}

	~actionset();
};
} // namespace xr
