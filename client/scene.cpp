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

#include "scene.h"
#include "application.h"
#include <cassert>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_raii.hpp>

scene::~scene() {}

scene::scene() :
	instance(application::instance().xr_instance),
	session(application::instance().xr_session),
	world_space(application::instance().world_space),
	viewconfig(application::instance().app_info.viewconfig),
	swapchains(application::instance().xr_swapchains),

	vk_instance(application::instance().vk_instance),
	device(application::instance().vk_device),
	physical_device(application::instance().vk_physical_device),
	queue(application::instance().vk_queue),
	commandpool(application::instance().vk_cmdpool)
{
}

void scene::on_unfocused() {}
void scene::on_focused() {}

vk::raii::Fence scene::create_fence(bool signaled)
{
	vk::FenceCreateFlags flags{0};

	if (signaled)
		flags = vk::FenceCreateFlagBits::eSignaled;

	return vk::raii::Fence(device, vk::FenceCreateInfo{.flags = flags});
}

vk::raii::Semaphore scene::create_semaphore()
{
	return vk::raii::Semaphore(device, {});
}
