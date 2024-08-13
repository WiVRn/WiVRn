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

#include "system.h"

#include "application.h"
#include "details/enumerate.h"
#include "openxr/openxr.h"
#include "utils/contains.h"
#include "vk/check.h"
#include "xr/check.h"
#include <cassert>
#include <openxr/openxr_platform.h>

xr::system::system(xr::instance & inst, XrFormFactor formfactor)
{
	if (!inst)
		throw std::invalid_argument("instance");

	this->inst = &inst;

	XrSystemGetInfo system_info{};
	system_info.type = XR_TYPE_SYSTEM_GET_INFO;
	system_info.formFactor = formfactor;
	CHECK_XR(xrGetSystem(inst, &system_info, &id));

	assert(id != XR_NULL_SYSTEM_ID);
}

XrGraphicsRequirementsVulkan2KHR xr::system::graphics_requirements() const
{
	auto xrGetVulkanGraphicsRequirements2KHR =
	        inst->get_proc<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");

	XrGraphicsRequirementsVulkan2KHR requirements{
	        .type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR,
	};

	CHECK_XR(xrGetVulkanGraphicsRequirements2KHR(*inst, id, &requirements));
	return requirements;
}

XrSystemProperties xr::system::properties() const
{
	if (!id)
		throw std::invalid_argument("this");

	XrSystemProperties prop{
	        .type = XR_TYPE_SYSTEM_PROPERTIES,
	};
	CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

	return prop;
}

XrSystemHandTrackingPropertiesEXT xr::system::hand_tracking_properties() const
{
	if (!id)
		throw std::invalid_argument("this");

	XrSystemHandTrackingPropertiesEXT hand_tracking_prop{
	        .type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
	};

	XrSystemProperties prop{
	        .type = XR_TYPE_SYSTEM_PROPERTIES,
	        .next = &hand_tracking_prop,
	};
	CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

	return hand_tracking_prop;
}

XrSystemEyeGazeInteractionPropertiesEXT xr::system::eye_gaze_interaction_properties() const
{
	if (!id)
		throw std::invalid_argument("this");

	XrSystemEyeGazeInteractionPropertiesEXT eye_gaze_prop{
	        .type = XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT,
	};

	XrSystemProperties prop{
	        .type = XR_TYPE_SYSTEM_PROPERTIES,
	        .next = &eye_gaze_prop,
	};
	CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

	return eye_gaze_prop;
}

XrSystemFaceTrackingProperties2FB xr::system::fb_face_tracking2_properties() const
{
	if (!id)
		throw std::invalid_argument("this");

	XrSystemFaceTrackingProperties2FB face_tracking_prop{
	        .type = XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES2_FB,
	};

	XrSystemProperties prop{
	        .type = XR_TYPE_SYSTEM_PROPERTIES,
	        .next = &face_tracking_prop,
	};
	CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

	return face_tracking_prop;
}

xr::system::passthrough_type xr::system::passthrough_supported() const
{
	if (utils::contains(environment_blend_modes(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO), XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND))
		return passthrough_type::color;

	const std::vector<std::string> & xr_extensions = application::get_xr_extensions();
	if (utils::contains(xr_extensions, XR_HTC_PASSTHROUGH_EXTENSION_NAME))
		return passthrough_type::color;

	if (utils::contains(xr_extensions, XR_FB_PASSTHROUGH_EXTENSION_NAME))
	{
		XrSystemPassthroughProperties2FB passthrough_prop2{
		        .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB,
		};

		XrSystemPassthroughPropertiesFB passthrough_prop{
		        .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB,
		        .next = &passthrough_prop2,
		};

		XrSystemProperties prop{
		        .type = XR_TYPE_SYSTEM_PROPERTIES,
		        .next = &passthrough_prop,
		};
		CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

		if (passthrough_prop.supportsPassthrough)
		{
			if (!(passthrough_prop2.capabilities & XR_PASSTHROUGH_CAPABILITY_BIT_FB))
				return passthrough_type::no_passthrough;

			if (passthrough_prop2.capabilities & XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB)
				return passthrough_type::color;

			return passthrough_type::bw;
		}
	}

	return passthrough_type::no_passthrough;
}

vk::raii::PhysicalDevice xr::system::physical_device(vk::raii::Instance & vulkan) const
{
	auto xrGetVulkanGraphicsDevice2KHR =
	        inst->get_proc<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");

	XrVulkanGraphicsDeviceGetInfoKHR get_info{
	        .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
	        .systemId = id,
	        .vulkanInstance = *vulkan,
	};

	VkPhysicalDevice dev;
	CHECK_XR(xrGetVulkanGraphicsDevice2KHR(*inst, &get_info, &dev));

	return vk::raii::PhysicalDevice{vulkan, dev};
}

vk::raii::Device xr::system::create_device(vk::raii::PhysicalDevice & pdev, vk::DeviceCreateInfo & create_info) const
{
	XrVulkanDeviceCreateInfoKHR xr_create_info{
	        .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
	        .systemId = id,
	        .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
	        .vulkanPhysicalDevice = *pdev,
	        .vulkanCreateInfo = &(VkDeviceCreateInfo &)create_info,
	};

	VkDevice dev;
	VkResult vresult;

	auto xrCreateVulkanDeviceKHR = inst->get_proc<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR");
	CHECK_XR(xrCreateVulkanDeviceKHR(*inst, &xr_create_info, &dev, &vresult));
	CHECK_VK(vresult, "xrCreateVulkanDeviceKHR");

	return vk::raii::Device{pdev, dev};
}

std::vector<XrViewConfigurationType> xr::system::view_configurations() const
{
	return details::enumerate<XrViewConfigurationType>(xrEnumerateViewConfigurations, *inst, id);
}

XrViewConfigurationProperties xr::system::view_configuration_properties(XrViewConfigurationType type) const
{
	XrViewConfigurationProperties prop{
	        .type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES,
	};
	CHECK_XR(xrGetViewConfigurationProperties(*inst, id, type, &prop));

	return prop;
}

std::vector<XrViewConfigurationView> xr::system::view_configuration_views(XrViewConfigurationType type) const
{
	return details::enumerate<XrViewConfigurationView>(xrEnumerateViewConfigurationViews, *inst, id, type);
}

std::vector<XrEnvironmentBlendMode> xr::system::environment_blend_modes(XrViewConfigurationType type) const
{
	return details::enumerate<XrEnvironmentBlendMode>(xrEnumerateEnvironmentBlendModes, *inst, id, type);
}
