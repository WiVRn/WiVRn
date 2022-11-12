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

#include "application.h"
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

	VkInstance vk_instance;
	VkDevice device;
	VkPhysicalDevice physical_device;
	VkQueue queue;
	vk::command_pool & commandpool;

	VkFence create_fence(bool signaled = true);
	VkSemaphore create_semaphore();

public:
	scene();

	virtual ~scene();
	virtual void before_render_view(XrViewStateFlags flags, XrTime predicted_display_time);
	virtual void render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index);

	virtual void render();
	virtual void on_unfocused();
	virtual void on_focused();
};
