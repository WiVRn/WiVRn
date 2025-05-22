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

#include "xr/pico_eye_types.h"
#include "xr/pico_eye_types_reflection.h"

#include <format>
#include <string>
#include <openxr/openxr_reflection.h>

namespace xr
{
#define XR_ENUM_CASE_STR(name, val) \
	case name:                  \
		return #name;
#define XR_ENUM_STR(enumType)                                                                 \
	constexpr const char * to_string(enumType e)                                          \
	{                                                                                     \
		switch (e)                                                                    \
		{                                                                             \
			XR_LIST_ENUM_##enumType(XR_ENUM_CASE_STR) default : return "Unknown"; \
		}                                                                             \
	}

XR_ENUM_STR(XrResult);
XR_ENUM_STR(XrFormFactor);
XR_ENUM_STR(XrViewConfigurationType);
XR_ENUM_STR(XrEnvironmentBlendMode);
XR_ENUM_STR(XrReferenceSpaceType);
XR_ENUM_STR(XrActionType);
XR_ENUM_STR(XrEyeVisibility);
XR_ENUM_STR(XrSessionState);
XR_ENUM_STR(XrObjectType);
XR_ENUM_STR(XrStructureType);

XR_ENUM_STR(XrTrackingStateCodePICO);
XR_ENUM_STR(XrBlendShapeIndexPICO);

inline std::string to_string(XrVersion version)
{
	return std::format("{}.{}.{}", XR_VERSION_MAJOR(version), XR_VERSION_MINOR(version), XR_VERSION_PATCH(version));
}

} // namespace xr
