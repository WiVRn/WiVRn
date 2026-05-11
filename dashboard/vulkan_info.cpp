/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "vulkan_info.h"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_to_string.hpp>

vulkan_info::vulkan_info()
{
	try
	{
		vk::raii::Context context;

		vk::ApplicationInfo app_info{"WiVRn dashboard", 1, "No engine", 1, VK_API_VERSION_1_1};
		vk::InstanceCreateInfo instance_info{vk::InstanceCreateFlags{0}, &app_info};
		vk::raii::Instance vulkan{context, instance_info};

		auto devices = vulkan.enumeratePhysicalDevices();
		set_info(choose_device(devices));
	}
	catch (std::runtime_error & e)
	{
		qCritical() << "Failed to get vulkan info: " << e.what();
	}
}

static vulkan_info::gpu_type cast_type(vk::PhysicalDeviceType type)
{
	switch (type)
	{
		case vk::PhysicalDeviceType::eOther:
			return vulkan_info::OtherGPU;
		case vk::PhysicalDeviceType::eIntegratedGpu:
			return vulkan_info::IGPU;
		case vk::PhysicalDeviceType::eDiscreteGpu:
			return vulkan_info::DGPU;
		case vk::PhysicalDeviceType::eVirtualGpu:
			return vulkan_info::VirtGPU;
		case vk::PhysicalDeviceType::eCpu:
			return vulkan_info::SoftGPU;
	}
	qCritical() << "invalid GPU type enum " << int(type);
	return vulkan_info::OtherGPU;
}

vk::raii::PhysicalDevice & vulkan_info::choose_device(std::vector<vk::raii::PhysicalDevice> & devices)
{
	for (vk::raii::PhysicalDevice & device: devices)
	{
		if (device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			return device;
	}

	return devices.front();
}

void vulkan_info::set_info(vk::raii::PhysicalDevice & device)
{
	auto [prop, driver_prop] = device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();

	m_driverId = QString::fromStdString(vk::to_string(driver_prop.driverID));

	m_driverVersionCode = prop.properties.driverVersion;
	switch (driver_prop.driverID)
	{
		case vk::DriverId::eNvidiaProprietary:
			m_driverVersion = QString("%1.%2.%3.%4")
			                          .arg((prop.properties.driverVersion >> 22) & 0x3ff)
			                          .arg((prop.properties.driverVersion >> 14) & 0xff)
			                          .arg((prop.properties.driverVersion >> 6) & 0xff)
			                          .arg(prop.properties.driverVersion & 0x3f);
			break;

		default:
			m_driverVersion = QString("%1.%2.%3")
			                          .arg(VK_VERSION_MAJOR(prop.properties.driverVersion))
			                          .arg(VK_VERSION_MINOR(prop.properties.driverVersion))
			                          .arg(VK_VERSION_PATCH(prop.properties.driverVersion));
			break;
	}

	m_type = cast_type(prop.properties.deviceType);

	qDebug() << "Driver" << m_driverId << m_driverVersion;
}
