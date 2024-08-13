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

#ifdef XR_USE_PLATFORM_ANDROID
#include <android_native_app_glue.h>
#endif

#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr
{
class instance;

class system
{
	instance * inst = nullptr;
	XrSystemId id = XR_NULL_SYSTEM_ID;

public:
	enum class passthrough_type
	{
		no_passthrough,
		bw,
		color
	};

	system() = default;
	system(const system &) = default;
	system & operator=(const system &) = default;
	system(xr::instance &, XrFormFactor formfactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY);

	operator XrSystemId() const
	{
		return id;
	}
	operator bool() const
	{
		return id != XR_NULL_SYSTEM_ID;
	}

	XrSystemProperties properties() const;
	XrSystemHandTrackingPropertiesEXT hand_tracking_properties() const;
	XrSystemEyeGazeInteractionPropertiesEXT eye_gaze_interaction_properties() const;
	XrSystemFaceTrackingProperties2FB fb_face_tracking2_properties() const;
	passthrough_type passthrough_supported() const;

	XrGraphicsRequirementsVulkan2KHR graphics_requirements() const;
	vk::raii::PhysicalDevice physical_device(vk::raii::Instance & vulkan) const;
	vk::raii::Device create_device(vk::raii::PhysicalDevice & pdev, vk::DeviceCreateInfo & create_info) const;

	std::vector<XrViewConfigurationType> view_configurations() const;
	XrViewConfigurationProperties view_configuration_properties(XrViewConfigurationType) const;
	std::vector<XrViewConfigurationView> view_configuration_views(XrViewConfigurationType) const;
	std::vector<XrEnvironmentBlendMode> environment_blend_modes(XrViewConfigurationType) const;
};
} // namespace xr
