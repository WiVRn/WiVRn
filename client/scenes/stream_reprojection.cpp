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

struct stream_reprojection::uniform
{
	// Foveation parameters
	alignas(8) glm::vec2 a;
	alignas(8) glm::vec2 b;
	alignas(8) glm::vec2 lambda;
	alignas(8) glm::vec2 xc;
};

const int nb_reprojection_vertices = 128;

stream_reprojection::stream_reprojection(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        std::vector<vk::Image> input_images_,
        std::vector<vk::Image> output_images_,
        vk::Extent2D extent,
        vk::Format format,
        const wivrn::to_headset::video_stream_description & description) :
        input_images(std::move(input_images_)),
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

	size_t uniform_size = sizeof(uniform) + properties.limits.minUniformBufferOffsetAlignment - 1;
	uniform_size = uniform_size - uniform_size % properties.limits.minUniformBufferOffsetAlignment;

	vk::BufferCreateInfo create_info{
	        .size = uniform_size * input_images.size(),
	        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
	        .sharingMode = vk::SharingMode::eExclusive,
	};

	VmaAllocationCreateInfo alloc_info{
	        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};

	buffer = buffer_allocation(device, create_info, alloc_info);
	void * data = buffer.map();
	for (size_t i = 0; i < input_images.size(); i++)
		ubo.push_back(reinterpret_cast<uniform *>(reinterpret_cast<uintptr_t>(data) + i * uniform_size));

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
	                .descriptorCount = (uint32_t)input_images.size(),
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = (uint32_t)input_images.size(),
	        },
	};

	vk::DescriptorPoolCreateInfo pool_info;
	pool_info.flags = vk::DescriptorPoolCreateFlags{};
	pool_info.maxSets = input_images.size();
	pool_info.setPoolSizes(pool_size);

	descriptor_pool = vk::raii::DescriptorPool(device, pool_info);

	// Create image views and descriptor sets
	input_image_views.reserve(input_images.size());
	descriptor_sets.reserve(input_images.size());
	VkDeviceSize offset = 0;
	for (vk::Image image: input_images)
	{
		vk::ImageViewCreateInfo iv_info{
		        .image = image,
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
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		};

		input_image_views.emplace_back(device, iv_info);

		vk::DescriptorSetAllocateInfo ds_info{
		        .descriptorPool = *descriptor_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &*descriptor_set_layout,
		};

		descriptor_sets.push_back(device.allocateDescriptorSets(ds_info)[0].release());

		vk::DescriptorImageInfo image_info{
		        .sampler = *sampler,
		        .imageView = *input_image_views.back(),
		        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};

		vk::DescriptorBufferInfo buffer_info{
		        .buffer = buffer,
		        .offset = offset,
		        .range = sizeof(uniform),
		};
		offset += uniform_size;

		std::array write{
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_sets.back(),
		                .dstBinding = 0,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .pImageInfo = &image_info,
		        },
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_sets.back(),
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

	vk::RenderPassCreateInfo renderpass_info;
	renderpass_info.setAttachments(attachment);
	renderpass_info.setSubpasses(subpass);

	renderpass = vk::raii::RenderPass(device, renderpass_info);

	// Create graphics pipeline
	vk::raii::ShaderModule vertex_shader = load_shader(device, "reprojection.vert");
	vk::raii::ShaderModule fragment_shader = load_shader(device, "reprojection.frag");

	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(*descriptor_set_layout);

	layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

	int specialization_constants[] = {
	        foveation_parameters[0].x.scale < 1,
	        foveation_parameters[0].y.scale < 1,
	        nb_reprojection_vertices,
	        nb_reprojection_vertices,
	};

	std::array specialization_constants_desc{
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

	vk::SpecializationInfo specialization_info;

	specialization_info.setMapEntries(specialization_constants_desc);
	specialization_info.setData<int>(specialization_constants);

	vk::pipeline_builder pipeline_info{
	        .flags = {},
	        .Stages = {
	                {
	                        .stage = vk::ShaderStageFlagBits::eVertex,
	                        .module = *vertex_shader,
	                        .pName = "main",
	                        .pSpecializationInfo = &specialization_info,
	                },
	                {
	                        .stage = vk::ShaderStageFlagBits::eFragment,
	                        .module = *fragment_shader,
	                        .pName = "main",
	                        .pSpecializationInfo = &specialization_info,
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
	output_image_views.reserve(output_images.size());
	framebuffers.reserve(output_images.size());
	for (vk::Image image: output_images)
	{
		vk::ImageViewCreateInfo iv_info{
		        .image = image,
		        .viewType = vk::ImageViewType::e2D,
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
		                .baseArrayLayer = 0,
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

void stream_reprojection::reproject(vk::raii::CommandBuffer & command_buffer, int source, int destination)
{
	if (source < 0 || source >= (int)input_images.size())
		throw std::runtime_error("Invalid source image index");
	if (destination < 0 || destination >= (int)output_images.size())
		throw std::runtime_error("Invalid destination image index");

	if (foveation_parameters[source].x.scale < 1)
	{
		ubo[source]->a.x = foveation_parameters[source].x.a;
		ubo[source]->b.x = foveation_parameters[source].x.b;
		ubo[source]->lambda.x = foveation_parameters[source].x.scale / foveation_parameters[source].x.a;
		ubo[source]->xc.x = foveation_parameters[source].x.center;
	}

	if (foveation_parameters[source].y.scale < 1)
	{
		ubo[source]->a.y = foveation_parameters[source].y.a;
		ubo[source]->b.y = foveation_parameters[source].y.b;
		ubo[source]->lambda.y = foveation_parameters[source].y.scale / foveation_parameters[source].y.a;
		ubo[source]->xc.y = foveation_parameters[source].y.center;
	}

	vk::ClearValue clear(vk::ClearColorValue(0, 0, 0, 0));
	vk::RenderPassBeginInfo begin_info{
	        .renderPass = *renderpass,
	        .framebuffer = *framebuffers[destination],
	        .renderArea = {
	                .offset = {0, 0},
	                .extent = extent,
	        },
	        .clearValueCount = 1,
	        .pClearValues = &clear,
	};

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
	                        .layerCount = 1,
	                },
	        });

	command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
	command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, descriptor_sets[source], {});
	command_buffer.draw(6 * nb_reprojection_vertices * nb_reprojection_vertices, 1, 0, 0);
	command_buffer.endRenderPass();
}

void stream_reprojection::set_foveation(std::array<wivrn::to_headset::foveation_parameter, 2> foveation)
{
	foveation_parameters = foveation;
}
