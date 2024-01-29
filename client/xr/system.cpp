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

#include "details/enumerate.h"
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

	XrGraphicsRequirementsVulkan2KHR requirements{};
	requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;

	CHECK_XR(xrGetVulkanGraphicsRequirements2KHR(*inst, id, &requirements));
	return requirements;
}

XrSystemProperties xr::system::properties() const
{
	if (!id)
		throw std::invalid_argument("this");

	XrSystemProperties prop{.type = XR_TYPE_SYSTEM_PROPERTIES};
	CHECK_XR(xrGetSystemProperties(*inst, id, &prop));

	return prop;
}

vk::raii::PhysicalDevice xr::system::physical_device(vk::raii::Instance& vulkan) const
{
	auto xrGetVulkanGraphicsDevice2KHR =
	        inst->get_proc<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");

	XrVulkanGraphicsDeviceGetInfoKHR get_info{};
	get_info.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
	get_info.systemId = id;
	get_info.vulkanInstance = *vulkan;

	VkPhysicalDevice dev;
	CHECK_XR(xrGetVulkanGraphicsDevice2KHR(*inst, &get_info, &dev));

	return vk::raii::PhysicalDevice{vulkan, dev};
}

vk::raii::Device xr::system::create_device(vk::raii::PhysicalDevice& pdev, vk::DeviceCreateInfo & create_info) const
{
	XrVulkanDeviceCreateInfoKHR xr_create_info{};

	xr_create_info.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
	xr_create_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
	xr_create_info.systemId = id;
	xr_create_info.vulkanPhysicalDevice = *pdev;
	xr_create_info.vulkanCreateInfo = &(VkDeviceCreateInfo&)create_info;

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
	XrViewConfigurationProperties prop{};
	prop.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
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
