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

#include "stream_defoveator.h"
#include "application.h"
#include "utils/ranges.h"
#include "vk/allocation.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include "vk/specialization_constants.h"
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

struct stream_defoveator::vertex
{
	// output image position
	alignas(8) glm::vec2 position;
	// input texture coordinates
	alignas(8) glm::uvec2 uv;
};

struct vert_pc
{
	glm::ivec4 rgb_rect;
	glm::ivec4 a_rect;
};

void stream_defoveator::ensure_vertices(size_t num_vertices)
{
	vk::BufferCreateInfo create_info{
	        .size = num_vertices * sizeof(vertex) * view_count,
	        .usage = vk::BufferUsageFlagBits::eVertexBuffer,
	        .sharingMode = vk::SharingMode::eExclusive,
	};

	if (create_info.size <= buffer.info().size)
		return;

	VmaAllocationCreateInfo alloc_info{
	        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};

	buffer = buffer_allocation(device, create_info, alloc_info);
	vertices_size = num_vertices * sizeof(vertex);
}

stream_defoveator::vertex * stream_defoveator::get_vertices(size_t view)
{
	assert(buffer);
	return reinterpret_cast<vertex *>(reinterpret_cast<uintptr_t>(buffer.map()) + view * vertices_size);
}

stream_defoveator::pipeline_t & stream_defoveator::ensure_pipeline(size_t view, vk::Sampler rgb, vk::Sampler a)
{
	auto & target = a ? pipeline_a[view] : pipeline_rgb[view];
	if (*target.pipeline)
		return target;

	std::array samplers{rgb, a};
	int32_t alpha = a ? 1 : 0;

	// Create VkDescriptorSetLayout
	std::array layout_binding{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = uint32_t(alpha + 1),
	                .stageFlags = vk::ShaderStageFlagBits::eFragment,
	                .pImmutableSamplers = samplers.data(),
	        },
	};

	target.descriptor_set_layout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
	        .bindingCount = layout_binding.size(),
	        .pBindings = layout_binding.data(),
	});

	target.ds = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
	        .descriptorPool = *ds_pool,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &*target.descriptor_set_layout,
	})[0]
	                    .release();

	// pipeline layout
	vk::PushConstantRange pc_range{
	        .stageFlags = vk::ShaderStageFlagBits::eVertex,
	        .size = sizeof(vert_pc),
	};

	vk::PipelineLayoutCreateInfo pipeline_layout_info{
	        .setLayoutCount = 1,
	        .pSetLayouts = &*target.descriptor_set_layout,
	        .pushConstantRangeCount = 1,
	        .pPushConstantRanges = &pc_range,
	};

	target.layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

	const auto & vk_device_extensions = application::get_vk_device_extensions();

	// Vertex shader
	auto vertex_shader = load_shader(device, "reprojection.vert");

	// Fragment shader
	auto specialization = make_specialization_constants(
	        int32_t(alpha),
	        VkBool32(need_srgb_conversion(guess_model())));
	auto fragment_shader = load_shader(device, "reprojection.frag");

	vk::pipeline_builder pipeline_info{
	        .flags = {},
	        .Stages = {
	                {
	                        .stage = vk::ShaderStageFlagBits::eVertex,
	                        .module = *vertex_shader,
	                        .pName = "main",
	                },
	                {
	                        .stage = vk::ShaderStageFlagBits::eFragment,
	                        .module = *fragment_shader,
	                        .pName = "main",
	                        .pSpecializationInfo = specialization,
	                },
	        },
	        .VertexBindingDescriptions = {
	                {
	                        .binding = 0,
	                        .stride = sizeof(vertex),
	                        .inputRate = vk::VertexInputRate::eVertex,
	                },
	        },
	        .VertexAttributeDescriptions = {
	                {
	                        .location = 0,
	                        .binding = 0,
	                        .format = vk::Format::eR32G32Sfloat,
	                        .offset = offsetof(vertex, position),
	                },
	                {
	                        .location = 1,
	                        .binding = 0,
	                        .format = vk::Format::eR32G32Uint,
	                        .offset = offsetof(vertex, uv),
	                },
	        },
	        .InputAssemblyState = {{
	                .topology = vk::PrimitiveTopology::eTriangleStrip,
	        }},
	        .Viewports = {{}},
	        .Scissors = {{}},
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
	        .DynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
	        .layout = *target.layout,
	        .renderPass = *renderpass,
	        .subpass = 0,
	};

	target.pipeline = device.createGraphicsPipeline(application::get_pipeline_cache(), pipeline_info);
	return target;
}

stream_defoveator::stream_defoveator(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        std::vector<vk::Image> output_images_,
        vk::Extent2D output_extent,
        vk::Format format) :
        device(device),
        physical_device(physical_device),
        output_images(std::move(output_images_)),
        output_extent(output_extent)
{
	// Create renderpass
	vk::AttachmentDescription attachment{
	        .format = format,
	        .samples = vk::SampleCountFlagBits::e1,
	        .loadOp = vk::AttachmentLoadOp::eDontCare,
	        .storeOp = vk::AttachmentStoreOp::eStore,
	        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentReference color_ref{
	        .attachment = 0,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};
	vk::SubpassDescription subpass{
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	        .colorAttachmentCount = 1,
	        .pColorAttachments = &color_ref,
	};

	vk::StructureChain renderpass_info{
	        vk::RenderPassCreateInfo{
	                .attachmentCount = 1,
	                .pAttachments = &attachment,
	                .subpassCount = 1,
	                .pSubpasses = &subpass,
	        },
	};

	renderpass = vk::raii::RenderPass(device, renderpass_info.get());

	vk::DescriptorPoolSize pool_size{
	        .type = vk::DescriptorType::eCombinedImageSampler,
	        .descriptorCount = view_count * 4,
	};

	ds_pool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{
	        .maxSets = view_count * 2,
	        .poolSizeCount = 1,
	        .pPoolSizes = &pool_size,
	});

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
			        .width = output_extent.width,
			        .height = output_extent.height,
			        .layers = 1,
			};
			fb_create_info.setAttachments(*output_image_views.back());

			framebuffers.emplace_back(device, fb_create_info);
		}
	}
}

static size_t required_vertices(const wivrn::to_headset::foveation_parameter & p)
{
	// strips are constructed like this:
	// 0 2 4
	// 1 3 5 5*
	// there is one such line per value in y
	// the last element is repeated to break the line
	return (2 * (p.x.size() + 1) + 1) * p.y.size();
}

void stream_defoveator::defoveate(vk::raii::CommandBuffer & command_buffer,
                                  const std::array<wivrn::to_headset::foveation_parameter, 2> & foveation,
                                  std::span<wivrn::blitter::output> inputs,
                                  int destination)
{
	if (destination < 0 || destination >= (int)output_images.size())
		throw std::runtime_error("Invalid destination image index");

	ensure_vertices(std::max(required_vertices(foveation[0]), required_vertices(foveation[1])));

	for (size_t view = 0; view < view_count; ++view)
	{
		const auto out_size = defoveated_size(foveation[view]);
		auto vertices = get_vertices(view);
		const auto & [px, py] = foveation[view];
		assert(px.size() % 2 == 1);
		assert(py.size() % 2 == 1);
		const int n_ratio_y = (py.size() - 1) / 2;
		const int n_ratio_x = (px.size() - 1) / 2;

		command_buffer.setScissor(
		        0,
		        vk::Rect2D{
		                .extent = {.width = uint32_t(out_size.width), .height = uint32_t(out_size.height)},
		        });
		command_buffer.setViewport(
		        0,
		        vk::Viewport{
		                .x = 0,
		                .y = 0,
		                .width = float(out_size.width),
		                .height = float(out_size.height),
		                .minDepth = 0,
		                .maxDepth = 1,
		        });

		glm::uvec2 in(0);
		glm::vec2 out(-0.5 * out_size.width, -0.5 * out_size.height); // pixel coordinates
		glm::vec2 out_pixel_size(2. / out_size.width,
		                         2. / out_size.height);
		for (auto [iy, n_out_y]: utils::enumerate_range(py))
		{
			// number of output pixels per source pixels
			const int ratio_y = std::abs(n_ratio_y - int(iy)) + 1;
			in.x = 0;
			out.x = -0.5 * out_size.width;
			for (auto [ix, n_out_x]: utils::enumerate_range(px))
			{
				const int ratio_x = std::abs(n_ratio_x - int(ix)) + 1;
				*vertices++ = {
				        .position = out * out_pixel_size,
				        .uv = in,
				};
				*vertices++ = {
				        .position = (out + glm::vec2(0, n_out_y * ratio_y)) * out_pixel_size,
				        .uv = (in + glm::uvec2(0, n_out_y)),
				};
				in.x += n_out_x;
				out.x += n_out_x * ratio_x;
			}
			*vertices++ = {
			        .position = out * out_pixel_size,
			        .uv = in,
			};
			in.y += n_out_y;
			out.y += n_out_y * ratio_y;
			*vertices++ = {
			        .position = out * out_pixel_size,
			        .uv = in,
			};
			*vertices++ = {
			        .position = out * out_pixel_size,
			        .uv = in,
			};
		}
	}

	for (size_t view = 0; view < view_count; ++view)
	{
		vk::RenderPassBeginInfo begin_info{
		        .renderPass = *renderpass,
		        .framebuffer = *framebuffers[destination * view_count + view],
		        .renderArea = {
		                .offset = {0, 0},
		                .extent = output_extent,
		        },
		};

		const auto & input = inputs[view];
		auto & pipeline = ensure_pipeline(view, input.sampler_rgb, input.sampler_a);

		std::array image_info{
		        vk::DescriptorImageInfo{
		                .sampler = input.sampler_rgb,
		                .imageView = input.rgb,
		                .imageLayout = input.layout_rgb,
		        },
		        vk::DescriptorImageInfo{
		                .sampler = input.sampler_a,
		                .imageView = input.a,
		                .imageLayout = input.layout_a,
		        },
		};

		std::array descriptor_writes{
		        vk::WriteDescriptorSet{
		                .dstSet = pipeline.ds,
		                .dstBinding = 0,
		                .descriptorCount = input.sampler_a ? 2u : 1u,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .pImageInfo = image_info.data(),
		        },
		};

		vert_pc pc{
		        .rgb_rect = glm::ivec4(input.rect_rgb.offset.x,
		                               input.rect_rgb.offset.y,
		                               input.rect_rgb.extent.width,
		                               input.rect_rgb.extent.height),
		        .a_rect = glm::ivec4(input.rect_a.offset.x,
		                             input.rect_a.offset.y,
		                             input.rect_a.extent.width,
		                             input.rect_a.extent.height),
		};

		device.updateDescriptorSets(descriptor_writes, {});

		command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.pipeline);
		command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, pipeline.ds, {});
		command_buffer.pushConstants<vert_pc>(*pipeline.layout, vk::ShaderStageFlagBits::eVertex, 0, pc);
		command_buffer.bindVertexBuffers(0, vk::Buffer(buffer), vertices_size * view);
		command_buffer.draw(required_vertices(foveation[view]), 1, 0, 0);
		command_buffer.endRenderPass();
	}
}

static uint16_t count_pixels(const std::vector<uint16_t> & param)
{
	uint16_t res = 0;
	const int n_ratio = (param.size() - 1) / 2;
	for (auto [i, n_out]: utils::enumerate_range(param))
	{
		// number of output pixels per source pixels
		const int ratio = std::abs(n_ratio - int(i)) + 1;
		res += ratio * n_out;
	}
	return res;
}

XrExtent2Di stream_defoveator::defoveated_size(const wivrn::to_headset::foveation_parameter & view) const
{
	return {
	        count_pixels(view.x),
	        count_pixels(view.y),
	};
}
