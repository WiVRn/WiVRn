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

#pragma once

#include <QObject>
#include <QtCore>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace vk::raii
{
class PhysicalDevice;
}

class vulkan_info : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(VulkanInfo)
	QML_SINGLETON
public:
	enum gpu_type
	{
		DGPU,
		IGPU,
		SoftGPU,
		VirtGPU,
		OtherGPU,
		NoGPU,
	};
	Q_ENUM(gpu_type)

	Q_PROPERTY(QString driverId READ driverId CONSTANT)
	Q_PROPERTY(QString driverVersion READ driverVersion CONSTANT)
	Q_PROPERTY(uint32_t driverVersionCode READ driverVersionCode CONSTANT)
	Q_PROPERTY(gpu_type type READ type CONSTANT)

	QString m_driverId;
	QString m_driverVersion;
	uint32_t m_driverVersionCode{};
	gpu_type m_type = NoGPU;

	vk::raii::PhysicalDevice & choose_device(std::vector<vk::raii::PhysicalDevice> & devices);
	void set_info(vk::raii::PhysicalDevice & device);

public:
	vulkan_info();

	QString driverId() const
	{
		return m_driverId;
	}

	QString driverVersion() const
	{
		return m_driverVersion;
	}

	uint32_t driverVersionCode() const
	{
		return m_driverVersionCode;
	}

	gpu_type type() const
	{
		return m_type;
	}
};
