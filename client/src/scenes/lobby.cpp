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

#include "lobby.h"
#include "glm/glm.hpp"
#include "render/text_rasterizer.h"
#include "stream.h"
#include "utils/ranges.h"
#include "utils/strings.h"
#include "vk/vk.h"

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <string>
#include <tiny_gltf.h>

scenes::lobby::~lobby() {}

scenes::lobby::lobby() :
        status_string_rasterizer(device, physical_device, commandpool, queue), discover("_wivrn._tcp.local."), renderer(device, physical_device, queue)
{
	// renderer.load_gltf("Lobby.gltf");

	uint32_t width = swapchains[0].width();
	uint32_t height = swapchains[0].height();

	// Create renderpass
	VkAttachmentReference color_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	vk::renderpass::info renderpass_info{.attachments = {VkAttachmentDescription{
						.format = swapchains[0].format(),
						.samples = VK_SAMPLE_COUNT_1_BIT,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					}},
					.subpasses = {VkSubpassDescription{
						.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						.colorAttachmentCount = 1,
						.pColorAttachments = &color_ref,
					}},
					.dependencies = {}};

	renderpass = vk::renderpass(device, renderpass_info);

	VkSamplerCreateInfo sampler_info{
	        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	        .magFilter = VK_FILTER_LINEAR,
	        .minFilter = VK_FILTER_LINEAR,
	        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	        .mipLodBias = 0.0f,
	        .anisotropyEnable = VK_FALSE,
	        .maxAnisotropy = 1,
	        .compareEnable = VK_FALSE,
	        .compareOp = VK_COMPARE_OP_NEVER,
	        .minLod = 0.0f,
	        .maxLod = 0.0f,
	        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	        .unnormalizedCoordinates = VK_FALSE,
	};

	CHECK_VK(vkCreateSampler(device, &sampler_info, nullptr, &status_string_sampler));

	// Create VkDescriptorSetLayout
	VkDescriptorSetLayoutBinding layout_binding{
	        .binding = 0,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	VkDescriptorSetLayoutCreateInfo layout_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	        .bindingCount = 1,
	        .pBindings = &layout_binding,
	};

	CHECK_VK(
	        vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &status_string_image_descriptor_set_layout));

	VkDescriptorPoolSize status_string_image_descriptor_set_pool_size{
	        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 1,
	};

	VkDescriptorPoolCreateInfo pool_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	        .flags = 0,
	        .maxSets = 1,
	        .poolSizeCount = 1,
	        .pPoolSizes = &status_string_image_descriptor_set_pool_size,
	};
	CHECK_VK(vkCreateDescriptorPool(device, &pool_info, nullptr, &status_string_descriptor_pool));

	VkDescriptorSetAllocateInfo ds_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	        .descriptorPool = status_string_descriptor_pool,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &status_string_image_descriptor_set_layout,
	};
	CHECK_VK(vkAllocateDescriptorSets(device, &ds_info, &status_string_image_descriptor_set));

	// Create graphics pipeline
	vk::shader vertex_shader(device, "text.vert");
	vk::shader fragment_shader(device, "text.frag");

	vk::pipeline::graphics_info pipeline_info;

	pipeline_info.shader_stages.resize(2);

	pipeline_info.shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	pipeline_info.shader_stages[0].module = vertex_shader;
	pipeline_info.shader_stages[0].pName = "main";

	pipeline_info.shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	pipeline_info.shader_stages[1].module = fragment_shader;
	pipeline_info.shader_stages[1].pName = "main";

	pipeline_info.InputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	pipeline_info.viewports.push_back(VkViewport{0, 0, (float)width, (float)height, 0.0f, 1.0f});

	pipeline_info.scissors.push_back(VkRect2D{{0, 0}, {width, height}});

	pipeline_info.RasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	pipeline_info.RasterizationState.lineWidth = 1;

	pipeline_info.MultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState pcbas{};
	pcbas.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

	pipeline_info.ColorBlendState.attachmentCount = 1;
	pipeline_info.ColorBlendState.pAttachments = &pcbas;

	pipeline_info.renderPass = renderpass;
	pipeline_info.subpass = 0;
	layout = vk::pipeline_layout(
	        device, {.descriptor_set_layouts = {status_string_image_descriptor_set_layout}, .push_constant_ranges = {VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(glm::mat4)}}});
	pipeline = vk::pipeline(device, pipeline_info, layout);

	images_data.resize(swapchains.size());
	for (size_t i = 0; i < swapchains.size(); i++)
	{
		auto & images = swapchains[i].images();
		images_data[i].resize(images.size());

		for (size_t j = 0; j < images.size(); j++)
		{
			VkFramebufferCreateInfo fb_create_info{};
			fb_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fb_create_info.renderPass = renderpass;
			fb_create_info.attachmentCount = 1;
			fb_create_info.pAttachments = &images[j].view;
			fb_create_info.width = swapchains[i].width();
			fb_create_info.height = swapchains[i].height();
			fb_create_info.layers = 1;
			CHECK_VK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &images_data[i][j].framebuffer));

			images_data[i][j].render_finished = create_semaphore();
		}
	}

	command_buffer = commandpool.allocate_command_buffer();
	fence = create_fence(false);
}

void scenes::lobby::rasterize_status_string()
{
	if (status_string_image_view)
		vkDestroyImageView(device, status_string_image_view, nullptr);

	status_string_rasterized_text = status_string_rasterizer.render(status_string);

	assert(status_string_rasterized_text.image != VK_NULL_HANDLE);
	assert(status_string_rasterized_text.memory != VK_NULL_HANDLE);

	VkImageViewCreateInfo iv_info{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	                              .image = status_string_rasterized_text.image,
	                              .viewType = VK_IMAGE_VIEW_TYPE_2D,
	                              .format = status_string_rasterized_text.format,
	                              .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
	                              .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	                                                   .baseMipLevel = 0,
	                                                   .levelCount = 1,
	                                                   .baseArrayLayer = 0,
	                                                   .layerCount = 1}};

	CHECK_VK(vkCreateImageView(device, &iv_info, nullptr, &status_string_image_view));

	VkDescriptorImageInfo image_info{
	        .sampler = status_string_sampler,
	        .imageView = status_string_image_view,
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet descriptor_write{
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = status_string_image_descriptor_set,
	        .dstBinding = 0,
	        .dstArrayElement = 0,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);

	last_status_string = status_string;
}

std::unique_ptr<wivrn_session> connect_to_session(const std::vector<wivrn_discover::service>& services)
{
	// TODO: make it asynchronous
	for(const wivrn_discover::service& service: services)
	{
		int port = service.port;
		for(const auto& address: service.addresses)
		{
			try
			{
				return std::visit([port](auto& address){
					return std::make_unique<wivrn_session>(address, port);
				}, address);
			}
			catch(std::exception& e)
			{
				spdlog::warn("Cannot connect to {}", service.hostname);
			}
		}
	}

	return {};
}

void scenes::lobby::render()
{
	if (next_scene && !next_scene->alive())
		next_scene.reset();

	if (!next_scene)
	{
		auto services = discover.get_services();

		if (auto session = connect_to_session(services))
		{
			next_scene = stream::create(std::move(session));
		}
	}

	if (next_scene)
	{
		if (next_scene->ready())
		{
			application::push_scene(next_scene);
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

	if (status_string != last_status_string)
		rasterize_status_string();

	XrFrameState framestate = session.wait_frame();

	if (!framestate.shouldRender)
	{
		session.begin_frame();
		session.end_frame(framestate.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();

	CHECK_VK(vkResetCommandBuffer(command_buffer, 0));

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	CHECK_VK(vkBeginCommandBuffer(command_buffer, &beginInfo));

	auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);
	assert(views.size() == swapchains.size());
	std::vector<XrCompositionLayerProjectionView> layer_view(views.size());

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
		layer_view[swapchain_index].subImage.imageRect.extent.width = swapchains[swapchain_index].width();
		layer_view[swapchain_index].subImage.imageRect.extent.height = swapchains[swapchain_index].height();
	}

	CHECK_VK(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command_buffer;

	CHECK_VK(vkQueueSubmit(queue, 1, &submitInfo, fence));

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

	CHECK_VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	CHECK_VK(vkResetFences(device, 1, &fence));
}

void scenes::lobby::render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index)
{
	xr::swapchain & swapchain = swapchains[swapchain_index];
	image_data & data = images_data[swapchain_index][image_index];

	VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderpass;
	renderPassInfo.framebuffer = data.framebuffer;
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = {swapchain.width(), swapchain.height()};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(command_buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &status_string_image_descriptor_set, 0, nullptr);

	float zn = 0.1;
	float r = tan(view.fov.angleRight);
	float l = tan(view.fov.angleLeft);
	float t = tan(view.fov.angleUp);
	float b = tan(view.fov.angleDown);

	// clang-format off
	glm::mat4 proj{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t), -1,   -1 },
		{ 0,           0,           -2*zn, 0 }
	};
	// clang-format on

	glm::mat4 view_matrix = glm::mat4_cast(glm::quat(view.pose.orientation.w, view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z));

	view_matrix = glm::translate(glm::mat4(1),
	                             glm::vec3(view.pose.position.x, view.pose.position.y, view.pose.position.z)) *
	              view_matrix;

	float aspect_ratio = (float)status_string_rasterized_text.size.width / (float)status_string_rasterized_text.size.height;
	glm::mat4 model_matrix{
	        {aspect_ratio, 0, 0, 0},
	        {0, 1, 0, 0},
	        {0, 0, 1, 0},
	        {-0.5 * aspect_ratio, -0.5, -10, 1}};

	glm::mat4 mvp = proj * glm::inverse(view_matrix) * model_matrix;
	vkCmdPushConstants(command_buffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

	vkCmdDraw(command_buffer, 6, 1, 0, 0);

	vkCmdEndRenderPass(command_buffer);
}
