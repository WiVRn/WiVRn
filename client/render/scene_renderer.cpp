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
#include "render/scene_components.h"
#include "utils/alignment.h"
#include "utils/fmt_glm.h"
#include "utils/ranges.h"
#include "vk/allocation.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include <boost/pfr/core.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <memory>
#include <ranges>
#include <spdlog/spdlog.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

// TODO move in lobby?
vk::Format scene_renderer::find_usable_image_format(
        vk::raii::PhysicalDevice physical_device,
        std::span<const vk::Format> formats,
        vk::Extent3D min_extent,
        vk::ImageUsageFlags usage,
        vk::ImageType type,
        vk::ImageTiling tiling,
        vk::ImageCreateFlags flags)
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

std::shared_ptr<renderer::texture> scene_renderer::create_default_texture(vk::raii::CommandPool & cb_pool, std::vector<uint8_t> pixel)
{
	vk::Format format;

	switch (pixel.size())
	{
		case 1:
			format = vk::Format::eR8Unorm;
			break;
		case 2:
			format = vk::Format::eR8G8Unorm;
			break;
		case 4:
			format = vk::Format::eR8G8B8A8Unorm;
			break;
		default:
			assert(false);
			__builtin_unreachable();
	}

	image_loader loader(physical_device, device, queue, cb_pool);
	loader.load(pixel, vk::Extent3D{1, 1, 1}, format);

	std::shared_ptr<vk::raii::ImageView> image_view = loader.image_view;

	return std::make_shared<renderer::texture>(image_view, renderer::sampler_info{});
}

std::shared_ptr<renderer::material> scene_renderer::create_default_material(vk::raii::CommandPool & cb_pool)
{
	auto default_material = std::make_shared<renderer::material>();
	default_material->name = "default";

	default_material->base_color_texture = create_default_texture(cb_pool, {255, 255, 255, 255});
	default_material->metallic_roughness_texture = create_default_texture(cb_pool, {255, 255});
	default_material->occlusion_texture = create_default_texture(cb_pool, {255});
	default_material->emissive_texture = create_default_texture(cb_pool, {0, 0, 0, 0});
	default_material->normal_texture = create_default_texture(cb_pool, {128, 128, 255, 255});

	default_material->buffer = std::make_shared<buffer_allocation>(
	        device,
	        vk::BufferCreateInfo{
	                .size = sizeof(default_material->staging),
	                .usage = vk::BufferUsageFlagBits::eUniformBuffer},
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO});
	memcpy(default_material->buffer->map(), &default_material->staging, sizeof(default_material->staging));
	default_material->buffer->unmap();

	return default_material;
}

static vk::FrontFace reverse(vk::FrontFace face)
{
	if (face == vk::FrontFace::eCounterClockwise)
		return vk::FrontFace::eClockwise;
	else
		return vk::FrontFace::eCounterClockwise;
}

static std::array layout_bindings_0{
        vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        },
};

static std::array layout_bindings_1{
        vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 3,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 4,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
                .binding = 5,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
};

scene_renderer::scene_renderer(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice physical_device,
        thread_safe<vk::raii::Queue> & queue,
        vk::raii::CommandPool & cb_pool,
        int frames_in_flight) :
        physical_device(physical_device),
        device(device),
        physical_device_properties(physical_device.getProperties()),
        queue(queue),
        layout_0(create_descriptor_set_layout(layout_bindings_0, vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)),
        layout_1(create_descriptor_set_layout(layout_bindings_1)),
        ds_pool_material(device, layout_1, layout_bindings_1, 100) // TODO tunable
{
	// Create the default material
	default_material = create_default_material(cb_pool);

	// Create Vulkan resources
	frame_resources.resize(frames_in_flight);

	std::vector<vk::raii::CommandBuffer> command_buffers = device.allocateCommandBuffers(
	        {
	                .commandPool = *cb_pool,
	                .level = vk::CommandBufferLevel::ePrimary,
	                .commandBufferCount = (uint32_t)frame_resources.size(),
	        });

	for (auto && [res, cb]: std::views::zip(frame_resources, command_buffers))
	{
		res.fence = device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
		res.cb = std::move(cb);
	}
	query_pool = vk::raii::QueryPool(
	        device,
	        vk::QueryPoolCreateInfo{
	                .queryType = vk::QueryType::eTimestamp,
	                .queryCount = uint32_t(2 * frames_in_flight),
	        });

	std::array layouts{*layout_0, *layout_1};
	pipeline_layout = create_pipeline_layout(layouts);
}

scene_renderer::~scene_renderer()
{
	wait_idle();
}

void scene_renderer::wait_idle()
{
	std::vector<vk::Fence> fences(frame_resources.size());
	for (auto && [i, f]: utils::enumerate(frame_resources))
		fences[i] = *f.fence;

	if (auto result = device.waitForFences(fences, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));
}

vk::raii::RenderPass & scene_renderer::get_renderpass(const renderpass_info & info)
{
	auto it = renderpasses.find(info);
	if (it != renderpasses.end())
		return it->second;

	return renderpasses.emplace(info, create_renderpass(info)).first->second;
}

vk::raii::RenderPass scene_renderer::create_renderpass(const renderpass_info & info_)
{
	vk::RenderPassCreateInfo info;

	std::array attachments = {
	        vk::AttachmentDescription{
	                .format = info_.color_format,
	                .samples = info_.msaa_samples,
	                .loadOp = vk::AttachmentLoadOp::eClear,
	                .storeOp = vk::AttachmentStoreOp::eStore,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	        },
	        vk::AttachmentDescription{
	                .format = info_.depth_format,
	                .samples = info_.msaa_samples,
	                .loadOp = vk::AttachmentLoadOp::eClear,
	                .storeOp = info_.keep_depth_buffer ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
	                .stencilLoadOp = vk::AttachmentLoadOp::eClear,
	                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
	        },
	        vk::AttachmentDescription{
	                .format = info_.color_format,
	                .samples = vk::SampleCountFlagBits::e1,
	                .loadOp = vk::AttachmentLoadOp::eDontCare,
	                .storeOp = vk::AttachmentStoreOp::eStore,
	                .stencilLoadOp = vk::AttachmentLoadOp::eClear,
	                .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
	                .initialLayout = vk::ImageLayout::eUndefined,
	                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,

	        }};

	if (info_.msaa_samples != vk::SampleCountFlagBits::e1)
	{
		info.setAttachments(attachments);
	}
	else
	{
		info.setPAttachments(attachments.data());
		info.setAttachmentCount(2);
	}

	vk::AttachmentReference color_attachment{
	        .attachment = 0,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentReference depth_attachment{
	        .attachment = 1,
	        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
	};

	// Only used if MSAA is enabled
	vk::AttachmentReference resolve_attachment{
	        .attachment = 2,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::SubpassDescription subpasses{
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	        .colorAttachmentCount = 1,
	        .pColorAttachments = &color_attachment,
	        .pDepthStencilAttachment = &depth_attachment,
	};

	if (info_.msaa_samples != vk::SampleCountFlagBits::e1)
		subpasses.pResolveAttachments = &resolve_attachment;

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

scene_renderer::output_image & scene_renderer::get_output_image_data(const output_image_info & info)
{
	auto it = output_images.find(info);
	if (it != output_images.end())
		return it->second;

	return output_images.emplace(info, create_output_image_data(info)).first->second;
}

scene_renderer::output_image scene_renderer::create_output_image_data(const output_image_info & info)
{
	output_image out;

	// TODO: use image view from xr::swapchain
	out.image_view = vk::raii::ImageView(
	        device, vk::ImageViewCreateInfo{
	                        .image = info.color,
	                        .viewType = vk::ImageViewType::e2D,
	                        .format = info.renderpass.color_format,
	                        .components{},
	                        .subresourceRange = {
	                                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                .baseMipLevel = 0,
	                                .levelCount = 1,
	                                .baseArrayLayer = info.base_array_layer,
	                                .layerCount = 1,
	                        },
	                });

	// TODO: check requiredFlags
	if (not info.depth)
		out.depth_buffer = image_allocation{
		        device,
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = info.renderpass.depth_format,
		                .extent = {
		                        .width = info.output_size.width,
		                        .height = info.output_size.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .samples = info.renderpass.msaa_samples,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		        }};

	out.depth_view = vk::raii::ImageView(
	        device,
	        vk::ImageViewCreateInfo{
	                .image = info.depth ? info.depth : out.depth_buffer,
	                .viewType = vk::ImageViewType::e2D,
	                .format = info.renderpass.depth_format,
	                .components{},
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eDepth,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = info.depth ? info.base_array_layer : 0,
	                        .layerCount = 1,
	                },
	        });

	if (info.renderpass.msaa_samples != vk::SampleCountFlagBits::e1)
	{
		out.multisample_image = image_allocation{
		        device,
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = info.renderpass.color_format,
		                .extent = {
		                        .width = info.output_size.width,
		                        .height = info.output_size.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .samples = info.renderpass.msaa_samples,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		                .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // TODO: check
		                .preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
		        }};

		out.multisample_view = vk::raii::ImageView(
		        device,
		        vk::ImageViewCreateInfo{
		                .image = out.multisample_image,
		                .viewType = vk::ImageViewType::e2D,
		                .format = info.renderpass.color_format,
		                .components{},
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .baseMipLevel = 0,
		                        .levelCount = 1,
		                        .baseArrayLayer = 0,
		                        .layerCount = 1,
		                },
		        });
	}

	vk::FramebufferCreateInfo fb_info{
	        .renderPass = *get_renderpass(info.renderpass),
	        .width = info.output_size.width,
	        .height = info.output_size.height,
	        .layers = 1,
	};

	if (info.renderpass.msaa_samples != vk::SampleCountFlagBits::e1)
	{
		std::array attachments{
		        vk::ImageView{*out.multisample_view},
		        vk::ImageView{*out.depth_view},
		        vk::ImageView{*out.image_view},
		};
		fb_info.setAttachments(attachments);
		out.framebuffer = vk::raii::Framebuffer(device, fb_info);
	}
	else
	{
		std::array attachments{
		        vk::ImageView{*out.image_view},
		        vk::ImageView{*out.depth_view},
		};
		fb_info.setAttachments(attachments);
		out.framebuffer = vk::raii::Framebuffer(device, fb_info);
	}

	return out;
}

vk::raii::Pipeline & scene_renderer::get_pipeline(const pipeline_info & info)
{
	auto it = pipelines.find(info);
	if (it != pipelines.end())
		return it->second;

	return pipelines.emplace(info, create_pipeline(info)).first->second;
}

vk::raii::DescriptorSetLayout scene_renderer::create_descriptor_set_layout(std::span<vk::DescriptorSetLayoutBinding> bindings, vk::DescriptorSetLayoutCreateFlags flags)
{
	vk::DescriptorSetLayoutCreateInfo dsl_info{.flags = flags};
	dsl_info.setBindings(bindings);
	return vk::raii::DescriptorSetLayout{device, dsl_info};
}

vk::raii::PipelineLayout scene_renderer::create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts)
{
	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(layouts);

	return vk::raii::PipelineLayout{device, pipeline_layout_info};
}

vk::raii::Pipeline scene_renderer::create_pipeline(const pipeline_info & info)
{
	auto vertex_description = renderer::vertex::describe();

	spdlog::debug("Creating pipeline");

	auto vertex_shader = load_shader(device, info.shader_name + ".vert");
	auto fragment_shader = load_shader(device, info.shader_name + ".frag");

	std::array specialization_constants_desc{
	        vk::SpecializationMapEntry{
	                .constantID = 0,
	                .offset = offsetof(pipeline_info, nb_texcoords),
	                .size = sizeof(int32_t),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 1,
	                .offset = offsetof(pipeline_info, dithering),
	                .size = sizeof(VkBool32),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 2,
	                .offset = offsetof(pipeline_info, alpha_cutout),
	                .size = sizeof(VkBool32),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 3,
	                .offset = offsetof(pipeline_info, skinning),
	                .size = sizeof(VkBool32),
	        }};

	vk::SpecializationInfo specialization{
	        .mapEntryCount = specialization_constants_desc.size(),
	        .pMapEntries = specialization_constants_desc.data(),
	        .dataSize = sizeof(pipeline_info),
	        .pData = &info,
	};

	return vk::raii::Pipeline{
	        device,
	        application::get_pipeline_cache(),
	        vk::pipeline_builder{
	                .Stages{
	                        vk::PipelineShaderStageCreateInfo{
	                                .stage = vk::ShaderStageFlagBits::eVertex,
	                                .module = *vertex_shader,
	                                .pName = "main",
	                                .pSpecializationInfo = &specialization,

	                        },
	                        vk::PipelineShaderStageCreateInfo{
	                                .stage = vk::ShaderStageFlagBits::eFragment,
	                                .module = *fragment_shader,
	                                .pName = "main",
	                                .pSpecializationInfo = &specialization,

	                        },
	                },
	                .VertexBindingDescriptions = {vertex_description.binding},
	                .VertexAttributeDescriptions = vertex_description.attributes,
	                .InputAssemblyState = {{
	                        .topology = info.topology,
	                        .primitiveRestartEnable = false,
	                }},
	                .Viewports = {vk::Viewport{}}, // Dynamic scissor / viewport but the count must be set
	                .Scissors = {vk::Rect2D{}},
	                .RasterizationState = {vk::PipelineRasterizationStateCreateInfo{
	                        .polygonMode = vk::PolygonMode::eFill,
	                        .cullMode = info.cull_mode,
	                        .frontFace = info.front_face,
	                        .lineWidth = 1.0,
	                }},
	                .MultisampleState = {vk::PipelineMultisampleStateCreateInfo{
	                        .rasterizationSamples = info.renderpass.msaa_samples,
	                }},
	                .DepthStencilState = {vk::PipelineDepthStencilStateCreateInfo{
	                        .depthTestEnable = info.depth_test_enable,
	                        .depthWriteEnable = info.depth_write_enable,
	                        .depthCompareOp = vk::CompareOp::eGreater,
	                        .depthBoundsTestEnable = false,
	                        .minDepthBounds = 0.0f,
	                        .maxDepthBounds = 1.0f,
	                }},
	                .ColorBlendState = {vk::PipelineColorBlendStateCreateInfo{}},
	                .ColorBlendAttachments = {vk::PipelineColorBlendAttachmentState{
	                        .blendEnable = info.blend_enable,
	                        .srcColorBlendFactor = vk::BlendFactor::eOne,
	                        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
	                        .colorBlendOp = vk::BlendOp::eAdd,
	                        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
	                        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
	                        .alphaBlendOp = vk::BlendOp::eAdd,
	                        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}},
	                .DynamicStates = {
	                        vk::DynamicState::eViewport,
	                        vk::DynamicState::eScissor,
	                },
	                .layout = *pipeline_layout,
	                .renderPass = *get_renderpass(info.renderpass),
	                .subpass = 0,
	        }};
}

vk::Sampler scene_renderer::get_sampler(const renderer::sampler_info & info)
{
	auto it = samplers.find(info);
	if (it != samplers.end())
		return **it->second;

	auto out = std::make_shared<vk::raii::Sampler>(
	        device,
	        vk::SamplerCreateInfo{
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

	auto & f = current_frame();
	if (auto result = device.waitForFences(*f.fence, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));
	device.resetFences(*f.fence);

	if (f.query_pool_filled)
	{
		auto [res, timestamps] = query_pool.getResults<uint64_t>(
		        current_frame_index * 2,
		        2,
		        2 * sizeof(uint64_t),
		        sizeof(uint64_t),
		        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

		if (res == vk::Result::eSuccess)
		{
			gpu_time_s = (timestamps[1] - timestamps[0]) * application::get_physical_device_properties().limits.timestampPeriod / 1e9;
		}
	}

	f.resources.clear();
	f.cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	f.cb.resetQueryPool(*query_pool, current_frame_index * 2, 2);
	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, current_frame_index * 2);

	f.uniform_buffer_offset = 0;

	if (!f.uniform_buffer)
	{
		// TODO multiple UBOs if it does not fit
		f.uniform_buffer = buffer_allocation{
		        device,
		        vk::BufferCreateInfo{
		                .size = 1048576,
		                .usage = vk::BufferUsageFlagBits::eUniformBuffer,
		        },
		        VmaAllocationCreateInfo{
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		        },
		        "scene_renderer::render (UBO)"};
	}
}

void scene_renderer::end_frame()
{
	auto & f = current_frame();

	f.cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, current_frame_index * 2 + 1);
	f.cb.end();

	queue.lock()->submit(vk::SubmitInfo{
	                             .commandBufferCount = 1,
	                             .pCommandBuffers = &*f.cb,
	                     },
	                     *f.fence);
	f.query_pool_filled = true;
}

scene_renderer::per_frame_resources & scene_renderer::current_frame()
{
	return frame_resources[current_frame_index];
}

void scene_renderer::update_material_descriptor_set(renderer::material & material)
{
	if (!material.ds || material.ds.use_count() != 1)
		material.ds = ds_pool_material.allocate();

	vk::DescriptorSet ds = **material.ds;

	auto f = [&](std::shared_ptr<renderer::texture> & texture) {
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
	        .range = sizeof(renderer::material::gpu_data),
	};

	// Write each descriptor separately because the HTC XR Elite needs it for some reason
	std::array write_ds{
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = 0,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = write_ds_image.data(),
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = 1,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = write_ds_image.data() + 1,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = 2,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = write_ds_image.data() + 2,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = 3,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = write_ds_image.data() + 3,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = 4,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = write_ds_image.data() + 4,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = ds,
	                .dstBinding = (uint32_t)write_ds_image.size(),
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eUniformBuffer,
	                .pBufferInfo = &write_ds_buffer,
	        },
	};

	device.updateDescriptorSets(write_ds, {});
}

static void print_scene_hierarchy(const entt::registry & scene, entt::entity root = entt::null, int level = 0)
{
	if (level == 0)
		spdlog::info("Node hierarchy:");

	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		if (node.parent != root)
			continue;

		// glm::mat4 M = model_matrices[index];
		// glm::vec4 pos = glm::column(M, 3);
		// spdlog::info("{:{}} {} pos={}, rot={}, pos to root={}", "", level * 2, node.name, node.translation, node.rotation, glm::vec3(pos));

		spdlog::info("{:{}} {} ({}, visible: {}, {})", "", level * 2, node.name, (int)entity, node.visible, node.global_visible);

		print_scene_hierarchy(scene, entity, level + 1);
	}
	if (level == 0)
		spdlog::info("---------------");
}

// Topological sort of all nodes
// static std::vector<entt::entity> sort_nodes(entt::registry & scene)
// {
// 	std::vector<std::pair<entt::entity, entt::entity>> unsorted; // first: child, second: parent
// 	std::unordered_set<entt::entity> processed;
// 	std::vector<entt::entity> sorted;
//
// 	for(auto && [entity, node]: scene.view<components::node>().each())
// 	{
// 		if (scene.valid(node.parent))
// 			unsorted.push_back({entity, node.parent});
// 		else
// 		{
// 			processed.emplace(entity);
// 			sorted.push_back(entity);
// 		}
// 	}
//
// 	while(not unsorted.empty())
// 	{
// 		for(auto it = unsorted.begin(); it != unsorted.end(); )
// 		{
// 			if (processed.contains(it->second))
// 			{
// 				processed.emplace(it->first);
// 				sorted.push_back(it->first);
// 				it = unsorted.erase(it);
// 			}
// 			else
// 				++it;
// 		}
// 	}
//
// 	return sorted;
// }

void scene_renderer::render(
        entt::registry & scene,
        const std::array<float, 4> & clear_color,
        uint32_t layer_mask,
        vk::Extent2D output_size,
        vk::Format color_format,
        vk::Format depth_format,
        vk::Image color_buffer,
        vk::Image depth_buffer,
        std::span<frame_info> frames)
{
	per_frame_resources & resources = current_frame();

	size_t buffer_alignment = std::max<size_t>(sizeof(glm::mat4), physical_device_properties.limits.minUniformBufferOffsetAlignment);
	// size_t buffer_alignment = std::max<size_t>(sizeof(glm::mat4), physical_device_properties.limits.minStorageBufferOffsetAlignment);

	vk::raii::CommandBuffer & cb = resources.cb;

	uint8_t * ubo = resources.uniform_buffer.data();

	std::array<vk::ClearValue, 2> clear_values{
	        vk::ClearColorValue{clear_color},
	        vk::ClearDepthStencilValue{0.0, 0},
	};

	// TODO once per frame, even if render is called several times
	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		glm::mat4 transform_to_root{1.0}; // Identity
		bool visible = true;
		bool reverse_side = false;
		uint32_t layers = -1;

		for (auto i = &node; i != nullptr and visible; i = scene.try_get<components::node>(i->parent))
		{
			float det = i->scale.x * i->scale.y * i->scale.z;
			glm::mat4 transform_to_parent = glm::translate(glm::mat4(1), i->position) * (glm::mat4)i->orientation * glm::scale(glm::mat4(1), i->scale);

			transform_to_root = transform_to_parent * transform_to_root;
			reverse_side = reverse_side ^ (det < 0);
			visible = visible and i->visible;
			layers = layers bitand i->layer_mask;
		}

		node.transform_to_root = transform_to_root;
		node.global_visible = visible;
		node.reverse_side = reverse_side;
		node.global_layer_mask = layers;
	}

	// print_scene_hierarchy(scene);

	for (const auto && [frame_index, frame]: utils::enumerate(frames))
	{
		scene_renderer::output_image & output = get_output_image_data(output_image_info{
		        .renderpass = {
		                .color_format = color_format,
		                .depth_format = depth_format,
		                .keep_depth_buffer = depth_buffer != vk::Image{},
		                .msaa_samples = vk::SampleCountFlagBits::e1,
		                // .msaa_samples = vk::SampleCountFlagBits::e4, // FIXME: MSAA does not work
		        },
		        .output_size = output_size,
		        .color = color_buffer,
		        .depth = depth_buffer,
		        .base_array_layer = (uint32_t)frame_index,
		});
		glm::mat4 viewproj = frame.projection * frame.view;

		vk::DeviceSize frame_ubo_offset = resources.uniform_buffer_offset;
		frame_gpu_data & frame_ubo = *reinterpret_cast<frame_gpu_data *>(ubo + resources.uniform_buffer_offset);
		resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(frame_gpu_data));

		// frame_ubo.ambient_color = glm::vec4(0.5,0.5,0.5,0); // TODO
		// frame_ubo.light_color = glm::vec4(0.5,0.5,0.5,0); // TODO

		// frame_ubo.ambient_color = glm::vec4(0.2,0.2,0.2,0); // TODO
		// frame_ubo.light_color = glm::vec4(0.8,0.8,0.8,0); // TODO

		frame_ubo.ambient_color = glm::vec4(0.1, 0.1, 0.1, 0); // TODO
		frame_ubo.light_color = glm::vec4(0.8, 0.8, 0.8, 0);   // TODO

		frame_ubo.light_position = glm::vec4(1, 1, 1, 0); // TODO
		frame_ubo.proj = frame.projection;
		frame_ubo.view = frame.view;

		vk::raii::RenderPass & renderpass = get_renderpass(renderpass_info{
		        .color_format = color_format,
		        .depth_format = depth_format,
		        .keep_depth_buffer = depth_buffer != vk::Image{},
		});

		cb.beginRenderPass(
		        vk::RenderPassBeginInfo{
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

		// Accumulate all visible primitives
		std::vector<std::tuple<bool, float, components::node *, renderer::primitive *>> primitives; // TODO keep it between frames
		for (auto && [entity, node]: scene.view<components::node>().each())
		{
			if (not node.global_visible or not node.mesh or (node.global_layer_mask & layer_mask) == 0)
				continue;

			// Position relative to the camera
			glm::vec4 position = frame.view * node.transform_to_root * glm::vec4(0, 0, 0, 1);

			for (renderer::primitive & primitive: node.mesh->primitives)
			{
				renderer::material & material = primitive.material_ ? *primitive.material_ : *default_material;
				primitives.emplace_back(material.blend_enable, position.z, &node, &primitive);
			}
		}

		// Sort by blending / distance
		// TODO add frustum culling
		std::ranges::stable_sort(primitives, [](const auto & a, const auto & b) -> bool {
			// Put the opaque objects first
			if (std::get<0>(a) < std::get<0>(b))
				return true;
			if (std::get<0>(a) > std::get<0>(b))
				return false;

			// If blending is disabled (std::get<0> == false), put the closest objects first
			// If blending is enabled (std::get<0> == true), put the farthest objeccts first
			return std::get<0>(a) ^ (std::get<1>(a) > std::get<1>(b));
		});

		// TODO try to add a depth pre-pass
		for (auto & [blend_enable, distance, node_ptr, primitive_ptr]: primitives)
		{
			components::node & node = *node_ptr;
			renderer::primitive & primitive = *primitive_ptr;

			glm::mat4 & transform = node.transform_to_root;

			// TODO: reuse the UBO if another primitive of the same mesh has already been drawn
			vk::DeviceSize instance_ubo_offset = resources.uniform_buffer_offset;
			instance_gpu_data & object_ubo = *reinterpret_cast<instance_gpu_data *>(ubo + resources.uniform_buffer_offset);
			resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(instance_gpu_data));

			vk::DeviceSize joints_ubo_offset = 0;
			if (!node.joints.empty())
			{
				joints_ubo_offset = resources.uniform_buffer_offset;
				glm::mat4 * joint_matrices = reinterpret_cast<glm::mat4 *>(ubo + resources.uniform_buffer_offset);
				resources.uniform_buffer_offset += utils::align_up(buffer_alignment, sizeof(glm::mat4) * 32);
				assert(node.joints.size() <= 32);

				for (auto && [idx, joint]: utils::enumerate(node.joints))
				{
					glm::mat4 & joint_transform = scene.get<components::node>(joint.first).transform_to_root;
					joint_matrices[idx] = glm::inverse(transform) * joint_transform * joint.second;
				}
			}

			object_ubo.model = transform;
			object_ubo.modelview = frame.view * transform;
			object_ubo.modelviewproj = viewproj * transform;
			object_ubo.clipping_planes = node.clipping_planes;

			// Get the material
			std::shared_ptr<renderer::material> material = primitive.material_ ? primitive.material_ : default_material;

			if (material->ds_dirty || !material->ds)
				update_material_descriptor_set(*material);

			// Get the pipeline
			pipeline_info info{
			        .renderpass = {
			                .color_format = color_format,
			                .depth_format = depth_format,
			                .keep_depth_buffer = depth_buffer != vk::Image{}},
			        .shader_name = material->shader_name,
			        .cull_mode = primitive.cull_mode,
			        .front_face = primitive.front_face,
			        .topology = primitive.topology,
			        .blend_enable = material->blend_enable,

			        .nb_texcoords = 2, // TODO
			        .skinning = !node.joints.empty(),
			};

			if (material->double_sided)
				info.cull_mode = vk::CullModeFlagBits::eNone;

			if (node.reverse_side)
				info.front_face = reverse(info.front_face);

			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *get_pipeline(info));

			cb.setViewport(0, vk::Viewport{
			                          .x = 0,
			                          .y = 0,
			                          .width = (float)output_size.width,
			                          .height = (float)output_size.height,
			                          .minDepth = 0,
			                          .maxDepth = 1,
			                  });

			cb.setScissor(0, vk::Rect2D{
			                         .offset = {0, 0},
			                         .extent = output_size,
			                 });

			if (primitive.indexed)
				cb.bindIndexBuffer(*node.mesh->buffer, primitive.index_offset, primitive.index_type);

			cb.bindVertexBuffers(0, (vk::Buffer)*node.mesh->buffer, primitive.vertex_offset);

			vk::DescriptorBufferInfo buffer_info_1{
			        .buffer = resources.uniform_buffer,
			        .offset = frame_ubo_offset,
			        .range = sizeof(frame_gpu_data)

			};
			vk::DescriptorBufferInfo buffer_info_2{
			        .buffer = resources.uniform_buffer,
			        .offset = instance_ubo_offset,
			        .range = sizeof(instance_gpu_data)};
			vk::DescriptorBufferInfo buffer_info_3{
			        .buffer = resources.uniform_buffer,
			        .offset = joints_ubo_offset,
			        .range = sizeof(glm::mat4) * 32};

			std::array descriptors{
			        vk::WriteDescriptorSet{
			                .dstBinding = 0,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_1,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 1,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_2,
			        },
			        vk::WriteDescriptorSet{
			                .dstBinding = 2,
			                .descriptorCount = 1,
			                .descriptorType = vk::DescriptorType::eUniformBuffer,
			                .pBufferInfo = &buffer_info_3,
			        },
			};

			cb.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, descriptors);

			// Set 1: material
			cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 1, **material->ds, {});

			if (primitive.indexed)
				cb.drawIndexed(primitive.index_count, 1, 0, 0, 0);
			else
				cb.draw(primitive.vertex_count, 1, 0, 0);

			resources.resources.push_back(material->ds);
		}
		cb.endRenderPass();
	}
}
