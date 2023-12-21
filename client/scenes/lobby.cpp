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
#include "render/text_rasterizer.h"
#include "stream.h"
#include "../common/version.h"
#include "application.h"

#include "vk/pipeline_layout.h"
#include "vk/shader.h"
#include "vk/pipeline.h"

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>

static const std::string discover_service = "_wivrn._tcp.local.";

scenes::lobby::~lobby() {}

scenes::lobby::lobby() :
        status_string_rasterizer(device, physical_device, commandpool, queue)//, renderer(device, physical_device, queue)
{
	uint32_t width = swapchains[0].width();
	uint32_t height = swapchains[0].height();

	// Create renderpass
	vk::RenderPassCreateInfo renderpass_info;

	vk::AttachmentDescription attachment_desc{
		.format = swapchains[0].format(),
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.initialLayout = vk::ImageLayout::eUndefined,
		.finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	};
	renderpass_info.setAttachments(attachment_desc);

	vk::SubpassDescription subpass_desc{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	};
	vk::AttachmentReference color_ref{
		.attachment = 0,
		.layout = vk::ImageLayout::eColorAttachmentOptimal,
	};
	subpass_desc.setColorAttachments(color_ref);
	renderpass_info.setSubpasses(subpass_desc);

	renderpass = vk::raii::RenderPass(device, renderpass_info);

	vk::SamplerCreateInfo sampler_info{
		.magFilter = vk::Filter::eLinear,
		.minFilter = vk::Filter::eLinear,
		.mipmapMode = vk::SamplerMipmapMode::eNearest,
		.addressModeU = vk::SamplerAddressMode::eClampToBorder,
		.addressModeV = vk::SamplerAddressMode::eClampToBorder,
		.addressModeW = vk::SamplerAddressMode::eClampToBorder,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1,
		.compareEnable = VK_FALSE,
		.compareOp = vk::CompareOp::eNever,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = vk::BorderColor::eFloatOpaqueBlack,
		.unnormalizedCoordinates = VK_FALSE,
	};

	status_string_sampler = vk::raii::Sampler(device, sampler_info);

	// Create descriptor set layout
	vk::DescriptorSetLayoutBinding layout_binding{
		.binding = 0,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eFragment,
	};

	vk::DescriptorSetLayoutCreateInfo layout_info;
	layout_info.setBindings(layout_binding);

	status_string_image_descriptor_set_layout = vk::raii::DescriptorSetLayout(device, layout_info);

	vk::DescriptorPoolSize status_string_image_descriptor_set_pool_size{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1,
	};

	vk::DescriptorPoolCreateInfo pool_info{
		.maxSets = 1,
	};
	pool_info.setPoolSizes(status_string_image_descriptor_set_pool_size);

	status_string_descriptor_pool = vk::raii::DescriptorPool(device, pool_info);

	vk::DescriptorSetAllocateInfo ds_info{
		.descriptorPool = *status_string_descriptor_pool,
		.descriptorSetCount = 1,
	};
	ds_info.setSetLayouts(*status_string_image_descriptor_set_layout);

	status_string_image_descriptor_set = device.allocateDescriptorSets(ds_info)[0].release();

	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(*status_string_image_descriptor_set_layout);
	vk::PushConstantRange push_constant_range{
		.stageFlags = vk::ShaderStageFlagBits::eVertex,
		.offset = 0,
		.size = sizeof(glm::mat4),
	};
	pipeline_layout_info.setPushConstantRanges(push_constant_range);

	layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

	// Create graphics pipeline
	vk::raii::ShaderModule vertex_shader = load_shader(device, "text.vert");
	vk::raii::ShaderModule fragment_shader = load_shader(device, "text.frag");

	vk::pipeline_builder pipeline_info
	{
		.flags = {},
		.Stages = {{
			.stage = vk::ShaderStageFlagBits::eVertex,
			.module = *vertex_shader,
			.pName = "main",
		},{
			.stage = vk::ShaderStageFlagBits::eFragment,
			.module = *fragment_shader,
			.pName = "main",
		}},
		.VertexInputState = {.flags = {}},
		.VertexBindingDescriptions = {},
		.VertexAttributeDescriptions = {},
		.InputAssemblyState = {{
			.topology = vk::PrimitiveTopology::eTriangleList,
		}},
		.ViewportState = {.flags = {}},
		.Viewports = {{
			.x = 0,
			.y = 0,
			.width = (float)width,
			.height = (float)height,
			.minDepth = 0,
			.maxDepth = 1,
		}},
		.Scissors = {{
			.offset = { .x = 0, .y = 0 },
			.extent= { .width = width, .height = height },
		}},
		.RasterizationState = {{
			.polygonMode = vk::PolygonMode::eFill,
			.lineWidth = 1,
		}},
		.MultisampleState = {{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
		}},
		.ColorBlendState = {.flags = {}},
		.ColorBlendAttachments = {{
			.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
		}},
		.layout = *layout,
		.renderPass = *renderpass,
		.subpass = 0,
	};

	pipeline = vk::raii::Pipeline(device, nullptr, pipeline_info);

	images_data.resize(swapchains.size());
	for (size_t i = 0; i < swapchains.size(); i++)
	{
		auto & images = swapchains[i].images();
		images_data[i].reserve(images.size());

		for (size_t j = 0; j < images.size(); j++)
		{
			vk::FramebufferCreateInfo fb_create_info{
				.renderPass = *renderpass,
				.attachmentCount = 1,
				.pAttachments = &*images[j].view,
				.width = swapchains[i].width(),
				.height = swapchains[i].height(),
				.layers = 1,
			};

			images_data[i].emplace_back(image_data{
				.framebuffer = vk::raii::Framebuffer(device, fb_create_info),
				.render_finished = create_semaphore()
			});
		}
	}

	vk::CommandBufferAllocateInfo alloc_info;
	alloc_info.commandBufferCount = 1;
	alloc_info.level = vk::CommandBufferLevel::ePrimary;
	alloc_info.commandPool = *commandpool;

	command_buffer = std::move(device.allocateCommandBuffers(alloc_info)[0]);

	application::set_debug_reports_name((VkCommandBuffer)*command_buffer, "lobby command buffer");
	fence = create_fence(false);
}

void scenes::lobby::rasterize_status_string()
{
	status_string_rasterized_text = status_string_rasterizer.render(status_string);

	vk::ImageViewCreateInfo iv_info{
		.image = (VkImage)status_string_rasterized_text.image,
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

std::unique_ptr<wivrn_session> connect_to_session(const std::vector<wivrn_discover::service>& services)
{
	char protocol_string[17];
	sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

	for(const wivrn_discover::service& service: services)
	{
		auto protocol = service.txt.find("protocol");
		if (protocol == service.txt.end())
			continue;

		if (protocol->second != protocol_string)
			continue;

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
				spdlog::warn("Cannot connect to {}: {}", service.hostname, e.what());
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

	command_buffer.reset();

	command_buffer.begin(vk::CommandBufferBeginInfo{});

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

	command_buffer.end();

	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*command_buffer);

	queue.submit(submit_info, *fence);

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

	if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout");
	device.resetFences(*fence);
}

void scenes::lobby::render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index)
{
	xr::swapchain & swapchain = swapchains[swapchain_index];
	image_data & data = images_data[swapchain_index][image_index];

	vk::ClearValue clear_color;
	clear_color.color = {0.0f, 0.0f, 0.0f, 1.0f};

	vk::RenderPassBeginInfo renderpass_info{
		.renderPass = *renderpass,
		.framebuffer = *data.framebuffer,
		.renderArea = {
			.offset = {0, 0},
			.extent = {swapchain.width(), swapchain.height()},
		}
	};
	renderpass_info.setClearValues(clear_color);

	command_buffer.beginRenderPass(renderpass_info, vk::SubpassContents::eInline);
	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
	command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, status_string_image_descriptor_set, {});

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

	command_buffer.pushConstants<glm::mat4>(*layout, vk::ShaderStageFlagBits::eVertex, 0, mvp);
	command_buffer.draw(6, 1, 0, 0);
	command_buffer.endRenderPass();
}

void scenes::lobby::on_focused()
{
	discover.emplace(discover_service);
}

void scenes::lobby::on_unfocused()
{
	discover.reset();
}
