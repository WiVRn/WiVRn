/*
 * WiVRn VR streaming
 * Copyright (C) 2023-2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "render/scene_renderer.h"

#include "application.h"
#include "render/growable_descriptor_pool.h"
#include "render/image_loader.h"
#include "render/scene_data.h"
#include "utils/alignment.h"
#include "utils/ranges.h"
#include "vk/allocation.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include <boost/pfr/core.hpp>
#include <chrono>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <initializer_list>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>
#include "utils/fmt_glm.h"
#include <vk_mem_alloc.h>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

static vk::Format find_usable_image_format(
        vk::raii::PhysicalDevice physical_device,
        std::span<vk::Format> formats,
        vk::Extent3D min_extent,
        vk::ImageUsageFlags usage,
        vk::ImageType type = vk::ImageType::e2D,
        vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
        vk::ImageCreateFlags flags = {})
{
	for (vk::Format format: formats)
	{
		try
		{
			vk::ImageFormatProperties prop = physical_device.getImageFormatProperties(format, type, tiling, usage, flags);

			if (prop.maxExtent.width < min_extent.width)
				continue;
			if (prop.maxExtent.height < min_extent.height)
				continue;
			if (prop.maxExtent.depth < min_extent.depth)
				continue;

			return format;
		}
		catch (vk::FormatNotSupportedError &)
		{
			continue;
		}
	}

	return vk::Format::eUndefined;
}

std::shared_ptr<scene_data::texture> scene_renderer::create_default_texture(vk::raii::CommandBuffer & cb, std::vector<buffer_allocation>& staging_buffers, std::initializer_list<float> pixel)
{
	vk::Format format;

	switch (pixel.size())
	{
		case 1:
			format = vk::Format::eR32Sfloat;
			break;
		case 2:
			format = vk::Format::eR32G32Sfloat;
			break;
		case 4:
			format = vk::Format::eR32G32B32A32Sfloat;
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}

	std::array<float, 4> pixel2;
	std::copy(pixel.begin(), pixel.end(), pixel2.begin());

	image_loader loader(device, cb, pixel2, vk::Extent3D{1, 1, 1}, format);

	auto image = std::make_shared<scene_data::image>(std::move(loader.image), std::move(loader.image_view));

	std::shared_ptr<vk::raii::ImageView> image_view(image, &image->image_view);

	staging_buffers.push_back(std::move(loader.staging_buffer));
	return std::make_shared<scene_data::texture>(image_view, sampler_info{});
}

std::shared_ptr<scene_data::material> scene_renderer::create_default_material(vk::raii::CommandPool & cb_pool)
{
	auto default_material = std::make_shared<scene_data::material>();
	default_material->name = "default";

	auto cb = std::move(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	        .commandPool = *cb_pool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);

	auto fence = device.createFence(vk::FenceCreateInfo{});

	cb.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	std::vector<buffer_allocation> staging_buffers;
	default_material->base_color_texture = create_default_texture(cb, staging_buffers, {1, 1, 1, 1});
	default_material->metallic_roughness_texture = create_default_texture(cb, staging_buffers, {1, 1});
	default_material->occlusion_texture = create_default_texture(cb, staging_buffers, {1});
	default_material->emissive_texture = create_default_texture(cb, staging_buffers, {0, 0, 0, 0});
	default_material->normal_texture = create_default_texture(cb, staging_buffers, {0.5, 0.5, 1, 1});

	cb.end();

	vk::SubmitInfo info;
	info.setCommandBuffers(*cb);

	queue.submit(info, *fence);

	device.waitForFences(*fence, true, 1'000'000'000); // TODO check for timeout

	default_material->buffer = std::make_shared<buffer_allocation>(
		device,
		vk::BufferCreateInfo{
			.size = sizeof(default_material->staging),
			.usage = vk::BufferUsageFlagBits::eUniformBuffer
		},
		VmaAllocationCreateInfo{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO
		}
	);
	memcpy(default_material->buffer->map(), &default_material->staging, sizeof(default_material->staging));
	default_material->buffer->unmap();

	return default_material;
}

vk::FrontFace reverse(vk::FrontFace face)
{
	if (face == vk::FrontFace::eCounterClockwise)
		return vk::FrontFace::eClockwise;
	else
		return vk::FrontFace::eCounterClockwise;
}

scene_renderer::scene_renderer(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, vk::raii::Queue & queue, vk::raii::CommandPool & cb_pool, vk::Extent2D output_size, vk::Format output_format, std::span<vk::Format> depth_formats, int frames_in_flight) :
        device(device),
        physical_device_properties(physical_device.getProperties()),
        queue(queue),
        output_size(output_size),
        output_format(output_format),
        depth_format(find_usable_image_format(physical_device, depth_formats, {output_size.width, output_size.height, 1}, vk::ImageUsageFlagBits::eDepthStencilAttachment)),
        ds_pool_frame(device, {
                                      .uniform_buffer = frames_in_flight,
                                      .uniform_buffer_dynamic = frames_in_flight,
                              },
                      frames_in_flight),
        ds_pool_material(device, {.combined_image_sampler = 500, .uniform_buffer = 100}, 100) // TODO tunable
{
	// Create the default material
	default_material = create_default_material(cb_pool);

	// Create Vulkan resources
	frame_resources.resize(frames_in_flight);

	std::vector<vk::raii::CommandBuffer> command_buffers = device.allocateCommandBuffers({
	        .commandPool = *cb_pool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = (uint32_t)frame_resources.size(),
	});

	for (auto && [res, cb]: utils::zip(frame_resources, command_buffers))
	{
		res.fence = device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
		res.cb = std::move(cb);

		res.query_pool = vk::raii::QueryPool(device, vk::QueryPoolCreateInfo{
			.queryType = vk::QueryType::eTimestamp,
			.queryCount = 2,
		});
	}

	renderpass = create_renderpass();

	// Create the descriptor set layouts
	std::array layout_bindings_0{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
	        vk::DescriptorSetLayoutBinding{
	                .binding = 1,
	                .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment}};
	vk::DescriptorSetLayoutCreateInfo dsl0_info{};
	dsl0_info.setBindings(layout_bindings_0);
	descriptor_set_frame = vk::raii::DescriptorSetLayout{device, dsl0_info};

	std::array layout_bindings_1{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = 5,
	                .stageFlags = vk::ShaderStageFlagBits::eFragment},
	        vk::DescriptorSetLayoutBinding{
	                .binding = 5,
	                .descriptorType = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eFragment}};
	vk::DescriptorSetLayoutCreateInfo dsl1_info{};
	dsl1_info.setBindings(layout_bindings_1);
	descriptor_set_material = vk::raii::DescriptorSetLayout{device, dsl1_info};

	std::array layouts{*descriptor_set_frame, *descriptor_set_material};
	pipeline_layout = create_pipeline_layout(layouts);

}

scene_renderer::~scene_renderer()
{
	wait_idle();
}

void scene_renderer::wait_idle()
{
	std::vector<vk::Fence> fences(frame_resources.size());
	for(auto&& [i, f]: utils::enumerate(frame_resources))
		fences[i] = *f.fence;

	device.waitForFences(fences, true, 1'000'000'000); // TODO check for timeout
}

// #define MSAA_4x

#ifdef MSAA_4x
#define MSAA_SAMPLES vk::SampleCountFlagBits::e4
#else
#define MSAA_SAMPLES vk::SampleCountFlagBits::e1
#endif

vk::raii::RenderPass scene_renderer::create_renderpass()
{
	vk::RenderPassCreateInfo info;

	std::array attachments = {
	        vk::AttachmentDescription{
	                .format = output_format,
	                .samples = MSAA_SAMPLES,
	                .loadOp = vk::AttachmentLoadOp::eClear,
	                .storeOp = vk::AttachmentStoreOp::eStore,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	        },
	        vk::AttachmentDescription{
	                .format = depth_format,
	                .samples = MSAA_SAMPLES,
	                .loadOp = vk::AttachmentLoadOp::eClear,
	                .storeOp = vk::AttachmentStoreOp::eDontCare,
	                .stencilLoadOp = vk::AttachmentLoadOp::eClear,
	                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,

	        },
#ifdef MSAA_4x
	        vk::AttachmentDescription{
	                .format = output_format,
	                .samples = vk::SampleCountFlagBits::e1,
	                .loadOp = vk::AttachmentLoadOp::eDontCare,
	                .storeOp = vk::AttachmentStoreOp::eStore,
	                .stencilLoadOp = vk::AttachmentLoadOp::eClear,
	                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,

	        }
#endif
	};
	info.setAttachments(attachments);

	vk::AttachmentReference color_attachment{
	        .attachment = 0,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentReference depth_attachment{
	        .attachment = 1,
	        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
	};

#ifdef MSAA_4x
	vk::AttachmentReference resolve_attachment{
	        .attachment = 2,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};
#endif

	vk::SubpassDescription subpasses{
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
#ifdef MSAA_4x
		.pResolveAttachments = &resolve_attachment,
#endif
	        .pDepthStencilAttachment = &depth_attachment,
	};
	info.setSubpasses(subpasses);

	std::array dependencies{
	        vk::SubpassDependency{
	                .srcSubpass = vk::SubpassExternal,
	                .dstSubpass = 0,
	                .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                .srcAccessMask{},
	                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	        },
	        vk::SubpassDependency{
	                .srcSubpass = vk::SubpassExternal,
	                .dstSubpass = 0,
	                .srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                .dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests,
	                .srcAccessMask{},
	                .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
	        }};
	info.setDependencies(dependencies);

	return vk::raii::RenderPass(device, info);
}

scene_renderer::output_image & scene_renderer::get_output_image_data(vk::Image output)
{
	auto it = output_images.find(output);
	if (it != output_images.end())
		return it->second;

	return output_images.emplace(output, create_output_image_data(output)).first->second;
}

scene_renderer::output_image scene_renderer::create_output_image_data(vk::Image output)
{
	output_image out;

	// TODO: use image view from xr::swapchain
	out.image_view = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
	                                                     .image = output,
	                                                     .viewType = vk::ImageViewType::e2D,
	                                                     .format = output_format,
	                                                     .components{},
	                                                     .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
	                                             });

	out.depth_buffer = image_allocation{device,
		                            vk::ImageCreateInfo{
	                                            .imageType = vk::ImageType::e2D,
	                                            .format = depth_format,
	                                            .extent = {
	                                                    .width = output_size.width,
	                                                    .height = output_size.height,
	                                                    .depth = 1,
	                                            },
	                                            .mipLevels = 1,
	                                            .arrayLayers = 1,
	                                            .samples = MSAA_SAMPLES,
	                                            .tiling = vk::ImageTiling::eOptimal,
	                                            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment

	                                    },
	                                    VmaAllocationCreateInfo{
	                                            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	                                            .usage = VMA_MEMORY_USAGE_AUTO,
	                                            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // TODO: check
	                                    }};

	out.depth_view = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
	                                                     .image = out.depth_buffer,
	                                                     .viewType = vk::ImageViewType::e2D,
	                                                     .format = depth_format,
	                                                     .components{},
	                                                     .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
	                                             });

#ifdef MSAA_4x
	out.multisample_image = image_allocation{vk::ImageCreateInfo{
	                                            .imageType = vk::ImageType::e2D,
	                                            .format = output_format,
	                                            .extent = {
	                                                    .width = output_size.width,
	                                                    .height = output_size.height,
	                                                    .depth = 1,
	                                            },
	                                            .mipLevels = 1,
	                                            .arrayLayers = 1,
	                                            .samples = MSAA_SAMPLES,
	                                            .tiling = vk::ImageTiling::eOptimal,
	                                            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment

	                                    },
	                                    VmaAllocationCreateInfo{
	                                            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
						    .usage = VMA_MEMORY_USAGE_AUTO,
	                                            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // TODO: check
						    .preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
	                                    }};
	out.multisample_view = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
	                                                     .image = out.multisample_image,
	                                                     .viewType = vk::ImageViewType::e2D,
	                                                     .format = output_format,
	                                                     .components{},
	                                                     .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
	                                             });
#endif

	vk::FramebufferCreateInfo fb_info{
	        .renderPass = *renderpass,
	        .width = output_size.width,
	        .height = output_size.height,
	        .layers = 1,
	};

#ifdef MSAA_4x
	std::array attachments{
		vk::ImageView{*out.multisample_view},
	        vk::ImageView{*out.depth_view},
	        vk::ImageView{*out.image_view},
	};
#else
	std::array attachments{
	        vk::ImageView{*out.image_view},
	        vk::ImageView{*out.depth_view},
	};
#endif
	fb_info.setAttachments(attachments);
	out.framebuffer = vk::raii::Framebuffer(device, fb_info);

	return out;
}

vk::raii::Pipeline & scene_renderer::get_pipeline(const pipeline_info & info)
{
	auto it = pipelines.find(info);
	if (it != pipelines.end())
		return it->second;

	return pipelines.emplace(info, create_pipeline(info)).first->second;
}

vk::raii::PipelineLayout scene_renderer::create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts)
{
	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(layouts);

	return vk::raii::PipelineLayout{device, pipeline_layout_info};
}

vk::raii::Pipeline scene_renderer::create_pipeline(const pipeline_info & info)
{
	auto vertex_description = scene_data::vertex::describe();

	spdlog::debug("Creating pipeline");

	auto vertex_shader = load_shader(device, info.shader_name + ".vert");
	auto fragment_shader = load_shader(device, info.shader_name + ".frag");

	return vk::raii::Pipeline{device, application::get_pipeline_cache(), vk::pipeline_builder{
	                                                   .Stages{
	                                                           vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = *vertex_shader, .pName = "main"},
	                                                           vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = *fragment_shader, .pName = "main"},
	                                                   },
	                                                   .VertexBindingDescriptions = {vertex_description.binding},
	                                                   .VertexAttributeDescriptions = vertex_description.attributes,
	                                                   .InputAssemblyState = {{
	                                                           .topology = info.topology,
	                                                           .primitiveRestartEnable = false,
	                                                   }},
	                                                   .Viewports = {vk::Viewport{
	                                                           .x = 0,
	                                                           .y = 0,
	                                                           .width = (float)output_size.width,
	                                                           .height = (float)output_size.height,
	                                                           .minDepth = 0,
	                                                           .maxDepth = 1,
	                                                   }},
	                                                   .Scissors = {vk::Rect2D{
	                                                           .offset = {0, 0},
	                                                           .extent = output_size,
	                                                   }},
	                                                   .RasterizationState = {vk::PipelineRasterizationStateCreateInfo{
	                                                           .polygonMode = vk::PolygonMode::eFill,
	                                                           .cullMode = info.cull_mode,
	                                                           .frontFace = info.front_face,
	                                                           .lineWidth = 1.0,
	                                                   }},
	                                                   .MultisampleState = {vk::PipelineMultisampleStateCreateInfo{
	                                                           .rasterizationSamples = MSAA_SAMPLES,
	                                                   }},
	                                                   .DepthStencilState = {vk::PipelineDepthStencilStateCreateInfo{
	                                                           .depthTestEnable = true,
	                                                           .depthWriteEnable = true,
	                                                           .depthCompareOp = vk::CompareOp::eLess,
								   .depthBoundsTestEnable = false,
								   .minDepthBounds = 0.0f,
								   .maxDepthBounds = 1.0f,
	                                                   }},
	                                                   .ColorBlendState = {vk::PipelineColorBlendStateCreateInfo{}},
	                                                   .ColorBlendAttachments = {vk::PipelineColorBlendAttachmentState{
								   .blendEnable = info.blend_enable,
								   .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
								   .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
								   .colorBlendOp = vk::BlendOp::eAdd,
								   .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
								   .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
								   .alphaBlendOp = vk::BlendOp::eAdd,
								   .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}},
	                                                   .DynamicState = {},
	                                                   .layout = *pipeline_layout,
	                                                   .renderPass = *renderpass,
	                                                   .subpass = 0,
	                                           }};
}

vk::Sampler scene_renderer::get_sampler(const sampler_info & info)
{
	auto it = samplers.find(info);
	if (it != samplers.end())
		return **it->second;

	auto out = std::make_shared<vk::raii::Sampler>(device, vk::SamplerCreateInfo{
	                                                               .magFilter = info.mag_filter,
	                                                               .minFilter = info.min_filter,
	                                                               .mipmapMode = info.min_filter_mipmap,
	                                                               .addressModeU = info.wrapS,
	                                                               .addressModeV = info.wrapT,
						// .anisotropyEnable = true,
						// .maxAnisotropy = 4,
	                                                       });

	samplers.emplace(info, out);
	return **out;
}

void scene_renderer::start_frame()
{
	current_frame_index = (current_frame_index + 1) % frame_resources.size();

	auto& f = current_frame();
	f.cpu_time_start = std::chrono::steady_clock::now();

	device.waitForFences(*f.fence, true, 1'000'000'000);
	device.resetFences(*f.fence);

	f.resources.clear();
	f.cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	f.cb.resetQueryPool(*f.query_pool, 0, 2);
	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *f.query_pool, 0);

	// TODO: replace with vector<byte> and write to GPU in end_frame()
	f.staging_buffer_offset = 0;
	if (!f.staging_buffer)
	{
		f.staging_buffer = buffer_allocation{device,
		                                     vk::BufferCreateInfo{
		                                             .size = 1048576,
		                                             .usage = vk::BufferUsageFlagBits::eUniformBuffer},
		                                     VmaAllocationCreateInfo{
		                                             .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		                                             .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE},
		                                     "scene_renderer::render (UBO staging)"};
	}
}

void scene_renderer::end_frame()
{
	auto& f = current_frame();

	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *f.query_pool, 1);
	f.cb.end();

	queue.submit(vk::SubmitInfo{
		.commandBufferCount = 1,
		.pCommandBuffers = &*f.cb,
	}, *f.fence);

	auto cpu_time_stop = std::chrono::steady_clock::now();
	spdlog::info("scene_renderer::render: CPU time: {} us", std::chrono::duration_cast<std::chrono::microseconds>(cpu_time_stop - f.cpu_time_start).count());

	auto [res, timestamps] = f.query_pool.getResults<uint64_t>(0, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
	if (res == vk::Result::eSuccess)
		spdlog::info("scene_renderer::render: GPU time: {} us", (int)((timestamps[1] - timestamps[0]) * physical_device_properties.limits.timestampPeriod * 1.0e-3));
}

scene_renderer::per_frame_resources & scene_renderer::current_frame()
{
	return frame_resources[current_frame_index];
}

void scene_renderer::update_material_descriptor_set(scene_data::material& material)
{
	if (!material.ds || !material.ds.unique())
		material.ds = ds_pool_material.allocate(descriptor_set_material);

	vk::DescriptorSet ds = **material.ds;

	auto f = [&](std::shared_ptr<scene_data::texture>& texture){
		return vk::DescriptorImageInfo{
			.sampler = get_sampler(texture->sampler),
			.imageView = **texture->image_view,
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};
	};

	std::array write_ds_image{
		f(material.base_color_texture),
		f(material.metallic_roughness_texture),
		f(material.occlusion_texture),
		f(material.emissive_texture),
		f(material.normal_texture),
	};

	vk::DescriptorBufferInfo write_ds_buffer{
		.buffer = *material.buffer,
		.offset = material.offset,
		.range = sizeof(scene_data::material::gpu_data)
	};

	std::array write_ds{
		vk::WriteDescriptorSet{
			.dstSet = ds,
			.dstBinding = 0,
			.descriptorCount = (uint32_t)write_ds_image.size(),
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = write_ds_image.data()
		},
		vk::WriteDescriptorSet{
			.dstSet = ds,
			.dstBinding = (uint32_t)write_ds_image.size(),
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.pBufferInfo = &write_ds_buffer
		},
	};

	device.updateDescriptorSets(write_ds, {});
}

static void print_scene_hierarchy(const scene_data& scene, std::span<glm::mat4> model_matrices, size_t root = scene_data::node::root_id, int level = 0)
{
	for(const auto&& [index, node]: utils::enumerate(scene.scene_objects))
	{
		if (node.parent_id != root)
			continue;

		// glm::mat4 M = model_matrices[index];
		// glm::vec4 pos = glm::column(M, 3);
		// spdlog::info("{:{}} {} pos={}, rot={}, pos to root={}", "", level * 2, node.name, node.translation, node.rotation, glm::vec3(pos));

		spdlog::info("{:{}} {}", "", level * 2, node.name);

		print_scene_hierarchy(scene, model_matrices, index, level + 1);
	}
}

void scene_renderer::render(scene_data & scene, const std::array<float, 4>& clear_color, std::span<frame_info> frames)
{
	per_frame_resources & resources = current_frame();

	size_t ubo_alignment = physical_device_properties.limits.minUniformBufferOffsetAlignment;

	size_t ubo_frame_size = utils::align_up(ubo_alignment, sizeof(frame_gpu_data));
	size_t ubo_instance_size = utils::align_up(ubo_alignment, sizeof(instance_gpu_data));

	size_t nb_frames = frames.size();
	//size_t nb_objects = scene.scene_objects.size();
	size_t nb_objects = std::count_if(scene.scene_objects.begin(), scene.scene_objects.end(), [](const scene_data::node& object){
		return object.mesh_id;
	});

	// Enough room for one frame_gpu_data per view and one instance_gpu_data per instance and per view
	// size_t ubo_size = nb_frames * (ubo_frame_size + nb_objects * ubo_instance_size);

	// Compute the position of each ubo, taking in account the minimum alignment
	std::vector<size_t> ubo_frame_offset(nb_frames);
	for(size_t i = 0; i < nb_frames; i++)
	{
		ubo_frame_offset[i] = resources.staging_buffer_offset;
		resources.staging_buffer_offset += ubo_frame_size;
	}

	std::vector<size_t> ubo_object_offset(nb_frames * nb_objects);
	for(size_t i = 0; i < nb_frames; i++)
	{
		for(size_t j = 0; j < nb_objects; j++)
		{
			ubo_object_offset[i * nb_objects + j] = resources.staging_buffer_offset;
			resources.staging_buffer_offset += ubo_instance_size;
		}
	}
	assert(resources.staging_buffer_offset <= resources.staging_buffer.info().size);

	auto current_ubo_frame_offset = ubo_frame_offset.begin();
	auto current_ubo_object_offset = ubo_object_offset.begin();

	vk::raii::CommandBuffer & cb = resources.cb;

	uintptr_t ubo = reinterpret_cast<uintptr_t>(resources.staging_buffer.map());

	std::array<vk::ClearValue, 2> clear_values{
	        vk::ClearColorValue{clear_color},
	        vk::ClearDepthStencilValue{1.0, 0},
	};

	// cb.copyBuffer(resources.staging_buffer, resources.uniform_buffer, {});

	auto vertex_layout = scene_data::vertex::describe();

	std::vector<glm::mat4> transform_to_root(scene.scene_objects.size());
	std::vector<bool> reverse_side(scene.scene_objects.size());
	std::vector<bool> visible(scene.scene_objects.size());


	for(const auto& [index, object]: utils::enumerate(scene.scene_objects))
	{
		glm::mat4 transform_to_parent = glm::translate(glm::mat4(1), object.position) * (glm::mat4)object.orientation * glm::scale(glm::mat4(1), object.scale);
		float det = object.scale.x * object.scale.y * object.scale.z;

		if (object.parent_id == scene_data::node::root_id)
		{
			transform_to_root[index] = transform_to_parent;
			reverse_side[index] = det < 0;
			visible[index] = object.visible;
		}
		else
		{
			size_t parent = object.parent_id;
			assert(parent < index);

			transform_to_root[index] = transform_to_root.at(parent) * transform_to_parent;

			reverse_side[index] = reverse_side[parent] ^ (det < 0);

			visible[index] = visible[parent] && object.visible;
		}
	}

	// print_scene_hierarchy(scene, transform_to_root);

	for (const auto && [frame_index, frame]: utils::enumerate(frames))
	{
		scene_renderer::output_image & output = get_output_image_data(frame.destination);
		glm::mat4 viewproj = frame.projection * frame.view;

		frame_gpu_data& frame_ubo = *reinterpret_cast<frame_gpu_data*>(ubo + *current_ubo_frame_offset);

		// frame_ubo.ambient_color = glm::vec4(0.5,0.5,0.5,0); // TODO
		// frame_ubo.light_color = glm::vec4(0.5,0.5,0.5,0); // TODO

		// frame_ubo.ambient_color = glm::vec4(0.2,0.2,0.2,0); // TODO
		// frame_ubo.light_color = glm::vec4(0.8,0.8,0.8,0); // TODO

		frame_ubo.ambient_color = glm::vec4(0,0,0,0); // TODO
		frame_ubo.light_color = glm::vec4(0.8,0.8,0.8,0); // TODO

		frame_ubo.light_position = glm::vec4(1,1,1,0); // TODO
		frame_ubo.proj = frame.projection;
		frame_ubo.view = frame.view;

		cb.beginRenderPass(vk::RenderPassBeginInfo{
		                           .renderPass = *renderpass,
		                           .framebuffer = *output.framebuffer,
		                           .renderArea = {
		                                   .offset = {0, 0},
		                                   .extent = output_size,
		                           },
		                           .clearValueCount = clear_values.size(),
		                           .pClearValues = clear_values.data(),
		                   },
		                   vk::SubpassContents::eInline);

		auto ds = ds_pool_frame.allocate(descriptor_set_frame);
		vk::DescriptorBufferInfo buffer_info_1{
			.buffer = resources.staging_buffer,
			.offset = (vk::DeviceSize)*current_ubo_frame_offset,
			.range = sizeof(frame_gpu_data)
		};;
		vk::DescriptorBufferInfo buffer_info_2{
			.buffer = resources.staging_buffer,
			.offset = 0,
			.range = sizeof(instance_gpu_data)
		};
		device.updateDescriptorSets(std::array{
			vk::WriteDescriptorSet{
				.dstSet = **ds,
				.dstBinding = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.pBufferInfo = &buffer_info_1,
			},
			vk::WriteDescriptorSet{
				.dstSet = **ds,
				.dstBinding = 1,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eUniformBufferDynamic,
				.pBufferInfo = &buffer_info_2,
			},
		}, {});
		resources.resources.push_back(ds);

		for(const auto& [index, object]: utils::enumerate(scene.scene_objects))
		{
			if (!object.mesh_id)
				continue;

			scene_data::mesh& mesh = scene.meshes.at(*object.mesh_id);

			instance_gpu_data& object_ubo = *reinterpret_cast<instance_gpu_data*>(ubo + *current_ubo_object_offset);

			glm::mat4& transform = transform_to_root[index];
			object_ubo.model = transform;
			object_ubo.modelview = frame.view * transform;
			object_ubo.modelviewproj = viewproj * transform;

			for(scene_data::primitive& primitive: mesh.primitives)
			{
				// Get the material
				std::shared_ptr<scene_data::material> material = primitive.material_ ? primitive.material_ : default_material;

				if (material->ds_dirty || !material->ds)
					update_material_descriptor_set(*material);

				// Get the pipeline
				pipeline_info info{
					.shader_name = material->shader_name,
					.cull_mode = primitive.cull_mode,
					.front_face = primitive.front_face,
					.topology = primitive.topology,
					.blend_enable = material->blend_enable,
				};

				if (material->double_sided)
					info.cull_mode = vk::CullModeFlagBits::eNone;

				if (reverse_side[index])
					info.front_face = reverse(info.front_face);

				cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *get_pipeline(info));

				if (primitive.indexed)
					cb.bindIndexBuffer(*mesh.buffer, primitive.index_offset, primitive.index_type);

				cb.bindVertexBuffers(0, (vk::Buffer)*mesh.buffer, primitive.vertex_offset);

				std::array<vk::DescriptorSet, 2> descriptor_sets{
					**ds,          // Set 0: per view and per object instance UBOs
					**material->ds // Set 1: material
				};

				std::array<uint32_t, 1> dynamic_offsets{
					(uint32_t)*current_ubo_object_offset // Offset of the object data
				};

				cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, descriptor_sets, dynamic_offsets);

				if (primitive.indexed)
					cb.drawIndexed(primitive.index_count, 1, 0, 0, 0);
				else
					cb.draw(primitive.vertex_count, 1, 0, 0);

				resources.resources.push_back(material->ds);
			}

			current_ubo_object_offset++;
		}
		cb.endRenderPass();

		current_ubo_frame_offset++;
	}
}
