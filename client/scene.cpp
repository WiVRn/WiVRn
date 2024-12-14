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
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_raii.hpp>

std::vector<scene::meta *> scene::scene_registry;

scene::~scene() {}

scene::scene(key, const meta & current_meta) :
        instance(application::instance().xr_instance),
        system(application::instance().xr_system_id),
        session(application::instance().xr_session),
        viewconfig(application::instance().app_info.viewconfig),

        vk_instance(application::instance().vk_instance),
        device(application::instance().vk_device),
        physical_device(application::instance().vk_physical_device),
        queue(application::instance().vk_queue),
        commandpool(application::instance().vk_cmdpool),
        queue_family_index(application::instance().vk_queue_family_index),
        current_meta(current_meta)
{
}
void scene::set_focused(bool status)
{
	if (status != focused)
	{
		focused = status;
		if (focused)
			on_focused();
		else
			on_unfocused();
	}
}

void scene::on_unfocused() {}
void scene::on_focused() {}
void scene::on_xr_event(const xr::event &) {}
