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

#include "stream_reprojection.h"
#include "application.h"
#include "utils/contains.h"
#include "vk/allocation.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

namespace
{
struct uniform
{
	// Foveation parameters
	alignas(8) glm::vec2 a;
	alignas(8) glm::vec2 b;
	alignas(8) glm::vec2 lambda;
	alignas(8) glm::vec2 xc;
};
} // namespace

const int nb_reprojection_vertices = 128;

struct SgsrSpecializationConstants
{
	VkBool32 use_edge_direction;
	float edge_threshold;
	float edge_sharpness;
};

stream_reprojection::stream_reprojection(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        vk::Image input_image_,
        uint32_t view_count,
        std::vector<vk::Image> output_images_,
        vk::Extent2D extent,
        vk::Format format,
        const wivrn::to_headset::video_stream_description & description) :
        view_count(view_count),
        input_image(input_image_),
        output_images(std::move(output_images_)),
        extent(extent)
{
	foveation_parameters = description.foveation;

	vk::PhysicalDeviceProperties properties = physical_device.getProperties();

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

	if (utils::contains(application::get_vk_device_extensions(), VK_IMG_FILTER_CUBIC_EXTENSION_NAME))
		sampler_info.magFilter = vk::Filter::eCubicIMG;

	sampler = vk::raii::Sampler(device, sampler_info);

	uniform_size = sizeof(uniform) + properties.limits.minUniformBufferOffsetAlignment - 1;
	uniform_size = uniform_size - uniform_size % properties.limits.minUniformBufferOffsetAlignment;

	vk::BufferCreateInfo create_info{
	        .size = uniform_size * view_count,
	        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
	        .sharingMode = vk::SharingMode::eExclusive,
	};

	VmaAllocationCreateInfo alloc_info{
	        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};

	buffer = buffer_allocation(device, create_info, alloc_info);

	// Create VkDescriptorSetLayout
	std::array layout_binding{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eFragment,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 1,
	                .descriptorType = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	        },
	};

	vk::DescriptorSetLayoutCreateInfo layout_info;
	layout_info.setBindings(layout_binding);

	descriptor_set_layout = vk::raii::DescriptorSetLayout(device, layout_info);

	std::array pool_size{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = (uint32_t)view_count,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = (uint32_t)view_count,
	        },
	};

	vk::DescriptorPoolCreateInfo pool_info{
	        .maxSets = view_count,
	        .poolSizeCount = pool_size.size(),
	        .pPoolSizes = pool_size.data(),
	};

	descriptor_pool = vk::raii::DescriptorPool(device, pool_info);

	// Create image views and descriptor sets
	for (uint32_t view = 0; view < view_count; ++view)
	{
		vk::ImageViewCreateInfo iv_info{
		        .image = input_image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = vk::Format::eA8B8G8R8SrgbPack32,
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
		                .baseArrayLayer = view,
		                .layerCount = 1,
		        },
		};

		vk::ImageView input_image_view = *input_image_views.emplace_back(device, iv_info);

		vk::DescriptorSetAllocateInfo ds_info{
		        .descriptorPool = *descriptor_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &*descriptor_set_layout,
		};

		auto descriptor_set = descriptor_sets.emplace_back(device.allocateDescriptorSets(ds_info)[0].release());

		vk::DescriptorImageInfo image_info{
		        .sampler = *sampler,
		        .imageView = input_image_view,
		        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};

		vk::DescriptorBufferInfo buffer_info{
		        .buffer = buffer,
		        .offset = uniform_size * view,
		        .range = sizeof(uniform),
		};

		std::array write{
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_set,
		                .dstBinding = 0,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .pImageInfo = &image_info,
		        },
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_set,
		                .dstBinding = 1,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eUniformBuffer,
		                .pBufferInfo = &buffer_info,
		        },
		};

		device.updateDescriptorSets(write, {});
	}

	// Create renderpass
	vk::AttachmentReference color_ref{
	        .attachment = 0,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentDescription attachment{
	        .format = format,
	        .samples = vk::SampleCountFlagBits::e1,
	        .loadOp = vk::AttachmentLoadOp::eDontCare,
	        .storeOp = vk::AttachmentStoreOp::eStore,
	        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
	        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::SubpassDescription subpass{
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	};
	subpass.setColorAttachments(color_ref);

	vk::StructureChain renderpass_info{
	        vk::RenderPassCreateInfo{
	                .attachmentCount = 1,
	                .pAttachments = &attachment,
	                .subpassCount = 1,
	                .pSubpasses = &subpass,
	        },
	};

	renderpass = vk::raii::RenderPass(device, renderpass_info.get());

	// Vertex shader
	vk::raii::ShaderModule vertex_shader = load_shader(device, "reprojection.vert");

	int vertex_specialization_constants[] = {
	        foveation_parameters[0].x.scale < 1,
	        foveation_parameters[0].y.scale < 1,
	        nb_reprojection_vertices,
	        nb_reprojection_vertices,
	};

	std::array vertex_specialization_constants_desc{
	        vk::SpecializationMapEntry{
	                .constantID = 0,
	                .offset = 0,
	                .size = sizeof(int),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 1,
	                .offset = sizeof(int),
	                .size = sizeof(int),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 2,
	                .offset = 2 * sizeof(int),
	                .size = sizeof(int),
	        },
	        vk::SpecializationMapEntry{
	                .constantID = 3,
	                .offset = 3 * sizeof(int),
	                .size = sizeof(int),
	        },
	};

	vk::SpecializationInfo vertex_specialization_info;
	vertex_specialization_info.setMapEntries(vertex_specialization_constants_desc);
	vertex_specialization_info.setData<int>(vertex_specialization_constants);

	// Fragment shader
	std::string fragment_shader_name = "reprojection.frag";

	vk::SpecializationInfo fragment_specialization_info;
	SgsrSpecializationConstants fragment_specialization_constants;
	std::vector<vk::SpecializationMapEntry> fragment_specialization_constants_desc;
	const configuration::sgsr_settings sgsr = application::get_config().sgsr;
	if (sgsr.enabled)
	{
		fragment_shader_name = "reprojection_sgsr.frag";

		fragment_specialization_constants = {
		        .use_edge_direction = sgsr.use_edge_direction,
		        .edge_threshold = float(sgsr.edge_threshold / 255.0),
		        .edge_sharpness = sgsr.edge_sharpness,
		};

		fragment_specialization_constants_desc = {
		        vk::SpecializationMapEntry{
		                .constantID = 0,
		                .offset = offsetof(SgsrSpecializationConstants, use_edge_direction),
		                .size = sizeof(fragment_specialization_constants.use_edge_direction),
		        },
		        vk::SpecializationMapEntry{
		                .constantID = 1,
		                .offset = offsetof(SgsrSpecializationConstants, edge_threshold),
		                .size = sizeof(fragment_specialization_constants.edge_threshold),
		        },
		        vk::SpecializationMapEntry{
		                .constantID = 2,
		                .offset = offsetof(SgsrSpecializationConstants, edge_sharpness),
		                .size = sizeof(fragment_specialization_constants.edge_sharpness),
		        },
		};

		fragment_specialization_info.setMapEntries(fragment_specialization_constants_desc);
		fragment_specialization_info.setDataSize(sizeof(fragment_specialization_constants));
		fragment_specialization_info.setPData(&fragment_specialization_constants);
	}

	vk::raii::ShaderModule fragment_shader = load_shader(device, fragment_shader_name);

	// Create graphics pipeline
	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(*descriptor_set_layout);

	layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

	vk::pipeline_builder pipeline_info{
	        .flags = {},
	        .Stages = {
	                {
	                        .stage = vk::ShaderStageFlagBits::eVertex,
	                        .module = *vertex_shader,
	                        .pName = "main",
	                        .pSpecializationInfo = &vertex_specialization_info,
	                },
	                {
	                        .stage = vk::ShaderStageFlagBits::eFragment,
	                        .module = *fragment_shader,
	                        .pName = "main",
	                        .pSpecializationInfo = &fragment_specialization_info,
	                },
	        },
	        .VertexBindingDescriptions = {},
	        .VertexAttributeDescriptions = {},
	        .InputAssemblyState = {{
	                .topology = vk::PrimitiveTopology::eTriangleList,
	        }},
	        .Viewports = {{
	                .x = 0,
	                .y = 0,
	                .width = (float)extent.width,
	                .height = (float)extent.height,
	                .minDepth = 0,
	                .maxDepth = 1,
	        }},
	        .Scissors = {{
	                .offset = {.x = 0, .y = 0},
	                .extent = extent,
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
	                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
	        }},
	        .layout = *layout,
	        .renderPass = *renderpass,
	        .subpass = 0,
	};

	pipeline = vk::raii::Pipeline(device, application::get_pipeline_cache(), pipeline_info);

	// Create image views and framebuffers
	output_image_views.reserve(output_images.size() * view_count);
	framebuffers.reserve(output_images.size() * view_count);
	for (vk::Image image: output_images)
	{
		for (uint32_t view = 0; view < view_count; ++view)
		{
			vk::ImageViewCreateInfo iv_info{
			        .image = image,
			        .viewType = vk::ImageViewType::e2DArray,
			        .format = format,
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
			                .baseArrayLayer = view,
			                .layerCount = 1,
			        },
			};

			output_image_views.emplace_back(device, iv_info);

			vk::FramebufferCreateInfo fb_create_info{
			        .renderPass = *renderpass,
			        .width = extent.width,
			        .height = extent.height,
			        .layers = 1,
			};
			fb_create_info.setAttachments(*output_image_views.back());

			framebuffers.emplace_back(device, fb_create_info);
		}
	}
}

void stream_reprojection::reproject(vk::raii::CommandBuffer & command_buffer, int destination)
{
	if (destination < 0 || destination >= (int)output_images.size())
		throw std::runtime_error("Invalid destination image index");

	for (size_t view = 0; view < view_count; ++view)
	{
		auto & ubo = *reinterpret_cast<uniform *>(reinterpret_cast<uintptr_t>(buffer.map()) + view * uniform_size);
		if (foveation_parameters[view].x.scale < 1)
		{
			ubo.a.x = foveation_parameters[view].x.a;
			ubo.b.x = foveation_parameters[view].x.b;
			ubo.lambda.x = foveation_parameters[view].x.scale / foveation_parameters[view].x.a;
			ubo.xc.x = foveation_parameters[view].x.center;
		}

		if (foveation_parameters[view].y.scale < 1)
		{
			ubo.a.y = foveation_parameters[view].y.a;
			ubo.b.y = foveation_parameters[view].y.b;
			ubo.lambda.y = foveation_parameters[view].y.scale / foveation_parameters[view].y.a;
			ubo.xc.y = foveation_parameters[view].y.center;
		}
	}

	command_buffer.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTopOfPipe,
	        vk::PipelineStageFlagBits::eColorAttachmentOutput,
	        {},
	        {},
	        {},
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = {},
	                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
	                .image = output_images[destination],
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .levelCount = 1,
	                        .layerCount = view_count,
	                },
	        });

	for (size_t view = 0; view < view_count; ++view)
	{
		vk::RenderPassBeginInfo begin_info{
		        .renderPass = *renderpass,
		        .framebuffer = *framebuffers[destination * view_count + view],
		        .renderArea = {
		                .offset = {0, 0},
		                .extent = extent,
		        },
		};

		command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
		command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, descriptor_sets[view], {});
		command_buffer.draw(6 * nb_reprojection_vertices * nb_reprojection_vertices, 1, 0, 0);
		command_buffer.endRenderPass();
	}
}

void stream_reprojection::set_foveation(std::array<wivrn::to_headset::foveation_parameter, 2> foveation)
{
	foveation_parameters = foveation;
}
