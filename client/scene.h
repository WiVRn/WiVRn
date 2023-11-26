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

#include <cstdint>

#include "xr/session.h"
#include "xr/swapchain.h"

class scene
{
protected:
	xr::instance & instance;
	xr::session & session;
	xr::space & world_space;
	XrViewConfigurationType viewconfig;
	std::vector<xr::swapchain> & swapchains;

	vk::raii::Instance& vk_instance;
	vk::raii::Device& device;
	vk::raii::PhysicalDevice& physical_device;
	vk::raii::Queue& queue;
	vk::raii::CommandPool& commandpool;

	vk::raii::Fence create_fence(bool signaled = true);
	vk::raii::Semaphore create_semaphore();

public:
	scene();

	virtual ~scene();

	virtual void render() = 0;
	virtual void on_unfocused();
	virtual void on_focused();
};
