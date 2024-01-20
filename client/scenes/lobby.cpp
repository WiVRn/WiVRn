/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "lobby.h"
#include "../common/version.h"
#include "application.h"
#include "input_profile.h"
#include "openxr/openxr.h"
#include "render/scene_data.h"
#include "render/text_rasterizer.h"
#include "stream.h"
#include "hardware.h"

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <utils/ranges.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>

static const std::string discover_service = "_wivrn._tcp.local.";

scenes::lobby::~lobby()
{
	if (renderer)
		renderer->wait_idle();
}

static std::string choose_webxr_profile()
{
#ifndef __ANDROID__
	const char * controller = std::getenv("WIVRN_CONTROLLER");
	if (controller && strcmp(controller, ""))
		return controller;
#endif

	switch(guess_model())
	{
		case model::oculus_quest:
			return "oculus-touch-v2";
		case model::oculus_quest_2:
			return "oculus-touch-v3";
		case model::meta_quest_pro:
			return "meta-quest-touch-pro";
		case model::meta_quest_3:
			return "meta-quest-touch-plus";
		case model::pico_neo_3:
			return "pico-neo3";
		case model::pico_4:
			return "pico-4";
		case model::unknown:
			return "generic-trigger-squeeze";
	}
}

scenes::lobby::lobby() :
        status_string_rasterizer(device, physical_device, commandpool, queue)
{
	uint32_t width = swapchains[0].width();
	uint32_t height = swapchains[0].height();
	vk::Extent2D output_size{width, height};

	std::array depth_formats{
	        vk::Format::eD16Unorm,
	        vk::Format::eX8D24UnormPack32,
	        vk::Format::eD32Sfloat,
	};

	renderer.emplace(device, physical_device, queue, commandpool, output_size, swapchains[0].format(), depth_formats);

	scene_loader loader(device, physical_device, queue, application::queue_family_index(), renderer->get_default_material());

	teapot.import(loader("lobby.gltf"));

	teapot.import(loader("imgui.gltf"));
	imgui_material = teapot.find_material("imgui");
	assert(imgui_material);

	input = input_profile("controllers/" + choose_webxr_profile() + "/profile.json", loader, teapot);

	spdlog::info("Loaded input profile {}", input->id);

	std::array imgui_inputs{
		imgui_context::controller{
			.aim     = application::left_aim(),
			.trigger = application::get_action("/user/hand/left/input/select/click"),
			.squeeze = application::get_action("/user/hand/left/input/squeeze/value"),
			.scroll  = application::get_action("/user/hand/left/input/thumbstick"),
		},
		imgui_context::controller{
			.aim     = application::right_aim(),
			.trigger = application::get_action("/user/hand/right/input/select/click"),
			.squeeze = application::get_action("/user/hand/right/input/squeeze/value"),
			.scroll  = application::get_action("/user/hand/right/input/thumbstick"),
		},
	};

	imgui_ctx.emplace(device, queue_family_index, queue, world_space, imgui_inputs);
}

void scenes::lobby::rasterize_status_string()
{
	status_string_rasterized_text = status_string_rasterizer.render(status_string);

	vk::ImageViewCreateInfo iv_info{
	        .image = status_string_rasterized_text.image,
	        .viewType = vk::ImageViewType::e2D,
	        .format = status_string_rasterized_text.format,
	        .components = {
	                .r = vk::ComponentSwizzle::eIdentity,
	                .g = vk::ComponentSwizzle::eIdentity,
	                .b = vk::ComponentSwizzle::eIdentity,
	                .a = vk::ComponentSwizzle::eIdentity,
	        },
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = 0,
	                .layerCount = 1,
	        },
	};

	status_string_image_view = vk::raii::ImageView(device, iv_info);

	vk::DescriptorImageInfo image_info{
	        .sampler = *status_string_sampler,
	        .imageView = *status_string_image_view,
	        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};

	vk::WriteDescriptorSet descriptor_write{
	        .dstSet = status_string_image_descriptor_set,
	        .dstBinding = 0,
	        .dstArrayElement = 0,
	        .descriptorCount = 1,
	        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	        .pImageInfo = &image_info,
	};

	device.updateDescriptorSets(descriptor_write, {});

	last_status_string = status_string;
}

std::unique_ptr<wivrn_session> connect_to_session(const std::vector<wivrn_discover::service> & services)
{
	char protocol_string[17];
	sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

	for (const wivrn_discover::service & service: services)
	{
		auto protocol = service.txt.find("protocol");
		if (protocol == service.txt.end())
			continue;

		if (protocol->second != protocol_string)
			continue;

		int port = service.port;
		for (const auto & address: service.addresses)
		{
			try
			{
				return std::visit([port](auto & address) {
					return std::make_unique<wivrn_session>(address, port);
				},
				                  address);
			}
			catch (std::exception & e)
			{
				spdlog::warn("Cannot connect to {}: {}", service.hostname, e.what());
			}
		}
	}

	return {};
}

static glm::mat4 projection_matrix(XrFovf fov, float zn = 0.1)
{
	float r = tan(fov.angleRight);
	float l = tan(fov.angleLeft);
	float t = tan(fov.angleUp);
	float b = tan(fov.angleDown);

	// clang-format off
	return glm::mat4{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t), -1,   -1 },
		{ 0,           0,           -2*zn, 0 }
	};
	// clang-format on
}

static glm::mat4 view_matrix(XrPosef pose)
{
	XrQuaternionf q = pose.orientation;
	XrVector3f pos = pose.position;

	glm::mat4 inv_view_matrix = glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));

	inv_view_matrix = glm::translate(glm::mat4(1), glm::vec3(pos.x, pos.y, pos.z)) * inv_view_matrix;

	return glm::inverse(inv_view_matrix);
}

void scenes::lobby::render()
{
	if (next_scene && !next_scene->alive())
		next_scene.reset();

	if (!next_scene)
	{
		auto services = discover->get_services();

		if (auto session = connect_to_session(services))
		{
			try
			{
				next_scene = stream::create(std::move(session));
			}
			catch (const std::exception & e)
			{
				spdlog::error("Failed to create stream session: {}", e.what());
			}
		}
	}

	if (next_scene)
	{
		if (next_scene->ready())
		{
			application::push_scene(next_scene);
			discover.reset();
			next_scene.reset();
		}
		else
		{
			status_string = "Waiting for video stream";
		}
	}
	else
	{
		status_string = "Waiting for connection";
	}

	// if (status_string != last_status_string)
	// rasterize_status_string();

	XrFrameState framestate = session.wait_frame();

	if (!framestate.shouldRender)
	{
		session.begin_frame();
		session.end_frame(framestate.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();

	auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);
	assert(views.size() == swapchains.size());

	input->apply(world_space, framestate.predictedDisplayTime);


	imgui_ctx->new_frame(framestate.predictedDisplayTime);
	ImGui::ShowDemoWindow();

	// Render the GUI to the imgui material
	imgui_material->base_color_texture->image_view = imgui_ctx->render();
	imgui_material->ds_dirty = true;

	std::vector<XrCompositionLayerProjectionView> layer_view;
	std::vector<scene_renderer::frame_info> frames;

	layer_view.reserve(views.size());
	frames.reserve(views.size());

	for (auto && [view, swapchain]: utils::zip(views, swapchains))
	{
		int image_index = swapchain.acquire();
		swapchain.wait();

		frames.push_back({.destination = swapchain.images()[image_index].image,
		                  .projection = projection_matrix(view.fov),
		                  .view = view_matrix(view.pose)});

		layer_view.push_back({
		        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		        .pose = view.pose,
		        .fov = view.fov,
		        .subImage = {
		                .swapchain = swapchain,
		                .imageRect = {
		                        .offset = {0, 0},
		                        .extent = swapchain.extent()}},
		});
	}

	assert(renderer);
	renderer->render(teapot, frames);

	for (auto & swapchain: swapchains)
		swapchain.release();

	XrCompositionLayerProjection layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = 0,
	        .space = world_space,
	        .viewCount = (uint32_t)layer_view.size(),
	        .views = layer_view.data(),
	};

	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));

	session.end_frame(framestate.predictedDisplayTime, layers_base);
}

void scenes::lobby::on_focused()
{
	discover.emplace(discover_service);
}

void scenes::lobby::on_unfocused()
{
	discover.reset();
}
