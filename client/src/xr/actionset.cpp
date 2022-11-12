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

#include "actionset.h"
#include "xr.h"
#include <cstring>

xr::actionset::actionset(xr::instance & inst, const std::string & name, const std::string & localized_name, uint32_t priority)
{
	this->inst = &inst;

	XrActionSetCreateInfo create_info{};
	create_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	strncpy(create_info.actionSetName, name.c_str(), sizeof(create_info.actionSetName) - 1);
	strncpy(create_info.localizedActionSetName, localized_name.c_str(), sizeof(create_info.localizedActionSetName) - 1);
	create_info.priority = priority;

	CHECK_XR(xrCreateActionSet(inst, &create_info, &id));
}

XrAction xr::actionset::create_action(XrActionType type, const std::string & name, const std::string & localized_name, const std::vector<std::string> & subactionpaths)
{
	std::vector<XrPath> paths(subactionpaths.size());
	for (size_t i = 0; i < subactionpaths.size(); i++)
	{
		paths[i] = inst->string_to_path(subactionpaths[i]);
	}

	XrActionCreateInfo create_info{};
	create_info.type = XR_TYPE_ACTION_CREATE_INFO;
	create_info.actionType = type,
	strncpy(create_info.actionName, name.c_str(), sizeof(create_info.actionName) - 1);
	strncpy(create_info.localizedActionName, localized_name.c_str(), sizeof(create_info.localizedActionName) - 1);
	create_info.countSubactionPaths = paths.size();
	create_info.subactionPaths = paths.data();

	XrAction action;
	CHECK_XR(xrCreateAction(id, &create_info, &action));

	return action;
}

xr::actionset::~actionset()
{
	if (id != XR_NULL_HANDLE)
		xrDestroyActionSet(id);
}
