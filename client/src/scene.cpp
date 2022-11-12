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

scene::~scene() {}

scene::scene() :
        instance(application::instance_->xr_instance),
        session(application::instance_->xr_session),
        world_space(application::instance_->world_space),
        viewconfig(application::instance_->app_info.viewconfig),
        swapchains(application::instance_->xr_swapchains),

        commandpool(application::instance_->vk_cmdpool)
{
	vk_instance = application::instance_->vk_instance;
	device = application::instance_->vk_device;
	physical_device = application::instance_->vk_physical_device;
	queue = application::instance_->vk_queue;
}

void scene::render()
{
	XrFrameState framestate = session.wait_frame();
	session.begin_frame();
	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	std::vector<XrCompositionLayerProjectionView> layer_view;
	XrCompositionLayerProjection layer{};

	if (framestate.shouldRender)
	{
		auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);

		assert(views.size() == swapchains.size());

		layer_view.resize(views.size());

		before_render_view(flags, framestate.predictedDisplayTime);
		for (size_t swapchain_index = 0; swapchain_index < views.size(); swapchain_index++)
		{
			int image_index = swapchains[swapchain_index].acquire();
			swapchains[swapchain_index].wait();

			render_view(flags, framestate.predictedDisplayTime, views[swapchain_index], swapchain_index, image_index);

			swapchains[swapchain_index].release();

			layer_view[swapchain_index].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			layer_view[swapchain_index].pose = views[swapchain_index].pose;
			layer_view[swapchain_index].fov = views[swapchain_index].fov;
			layer_view[swapchain_index].subImage.swapchain = swapchains[swapchain_index];
			layer_view[swapchain_index].subImage.imageRect.offset = {0, 0};
			layer_view[swapchain_index].subImage.imageRect.extent.width =
			        swapchains[swapchain_index].width();
			layer_view[swapchain_index].subImage.imageRect.extent.height =
			        swapchains[swapchain_index].height();
		}

		layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		layer.space = world_space;
		layer.layerFlags = 0;
		layer.viewCount = layer_view.size();
		layer.views = layer_view.data();
		layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));
	}

	session.end_frame(framestate.predictedDisplayTime, layers_base);
}

void scene::before_render_view(XrViewStateFlags flags, XrTime predicted_display_time) {}

void scene::render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index)
{}

void scene::on_unfocused() {}
void scene::on_focused() {}

VkFence scene::create_fence(bool signaled)
{
	// TODO: RAII
	VkFenceCreateInfo fence_info{};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

	VkFence fence;
	CHECK_VK(vkCreateFence(device, &fence_info, nullptr, &fence));

	return fence;
}

VkSemaphore scene::create_semaphore()
{
	// TODO: RAII
	VkSemaphoreCreateInfo sem_info{};
	sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	CHECK_VK(vkCreateSemaphore(device, &sem_info, nullptr, &semaphore));

	return semaphore;
}
