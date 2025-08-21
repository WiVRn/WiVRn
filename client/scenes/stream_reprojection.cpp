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

struct stream_reprojection::vertex
{
	// output image position
	alignas(8) glm::vec2 position;
	// input texture coordinates
	alignas(8) glm::vec2 uv;
	int32_t shading_rate;
};

void stream_reprojection::ensure_vertices(size_t num_vertices)
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

stream_reprojection::vertex * stream_reprojection::get_vertices(size_t view)
{
	assert(buffer);
	return reinterpret_cast<vertex *>(reinterpret_cast<uintptr_t>(buffer.map()) + view * vertices_size);
}

stream_reprojection::stream_reprojection(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        image_allocation & input_image,
        std::vector<vk::Image> output_images_,
        vk::Extent2D output_extent,
        vk::Format format) :
        view_count(input_image.info().arrayLayers),
        device(device),
        input_extent{
                .width = input_image.info().extent.width,
                .height = input_image.info().extent.height,
        },
        output_images(std::move(output_images_)),
        output_extent(output_extent)
{
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

	// Create VkDescriptorSetLayout
	vk::DescriptorSetLayoutBinding layout_binding{
	        .binding = 0,
	        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	        .descriptorCount = 1,
	        .stageFlags = vk::ShaderStageFlagBits::eFragment,
	};

	vk::DescriptorSetLayoutCreateInfo layout_info;
	layout_info.setBindings(layout_binding);

	descriptor_set_layout = vk::raii::DescriptorSetLayout(device, layout_info);

	std::array pool_size{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
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
		        .format = input_image.info().format,
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

		device.updateDescriptorSets(
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_set,
		                .dstBinding = 0,
		                .dstArrayElement = 0,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .pImageInfo = &image_info,
		        },
		        {});
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

	// Fragment shader

	const configuration::sgsr_settings & sgsr = application::get_config().sgsr;
	auto sgsr_specialization = make_specialization_constants(
	        VkBool32(sgsr.use_edge_direction),
	        float(sgsr.edge_threshold / 255.f),
	        float(sgsr.edge_sharpness));

	vk::raii::ShaderModule fragment_shader = load_shader(device, sgsr.enabled ? "reprojection_sgsr.frag" : "reprojection.frag");

	// Create graphics pipeline
	vk::PipelineLayoutCreateInfo pipeline_layout_info;
	pipeline_layout_info.setSetLayouts(*descriptor_set_layout);

	layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

	vk::pipeline_builder pipeline_info_builder{
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
	                        .pSpecializationInfo = sgsr.enabled ? sgsr_specialization.info() : nullptr,
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
	                        .format = vk::Format::eR32G32Sfloat,
	                        .offset = offsetof(vertex, uv),
	                },
	                {
	                        .location = 2,
	                        .binding = 0,
	                        .format = vk::Format::eR32Sint,
	                        .offset = offsetof(vertex, shading_rate),
	                },
	        },
	        .InputAssemblyState = {{
	                .topology = vk::PrimitiveTopology::eTriangleStrip,
	        }},
	        .Viewports = {{
	                .x = 0,
	                .y = 0,
	                .width = (float)output_extent.width,
	                .height = (float)output_extent.height,
	                .minDepth = 0,
	                .maxDepth = 1,
	        }},
	        .Scissors = {{
	                .offset = {.x = 0, .y = 0},
	                .extent = output_extent,
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

	vk::GraphicsPipelineCreateInfo pipeline_info(pipeline_info_builder);

	// Variable fragment shading
	std::array combiner{
	        vk::FragmentShadingRateCombinerOpKHR::eMax,
	        vk::FragmentShadingRateCombinerOpKHR::eMax,
	};
	vk::PipelineFragmentShadingRateStateCreateInfoKHR shading{
	        .fragmentSize = vk::Extent2D{1, 1},
	        .combinerOps = combiner,
	};
	const auto & vk_device_extensions = application::get_vk_device_extensions();
	if (utils::contains(vk_device_extensions, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) and
	    utils::contains(vk_device_extensions, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME))
	{
		const auto & [prop, rate_prop] = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceFragmentShadingRatePropertiesKHR>();
		const auto & [feat, fragment_feat] = physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceFragmentShadingRateFeaturesKHR>();
		if (rate_prop.fragmentShadingRateNonTrivialCombinerOps and
		    fragment_feat.primitiveFragmentShadingRate and
		    fragment_feat.attachmentFragmentShadingRate)
		{
			pipeline_info.pNext = &shading;

			spdlog::info("Available fragment shading rates:");
			for (const auto rate: physical_device.getFragmentShadingRatesKHR())
			{
				if (rate.sampleCounts & vk::SampleCountFlagBits::e1)
				{
					spdlog::info("\tfragment size: {}x{}", rate.fragmentSize.width, rate.fragmentSize.height);
					int flags = 0;
					if (rate.fragmentSize.width == 4)
						flags |= 8;
					else if (rate.fragmentSize.width == 2)
						flags |= 4;
					if (rate.fragmentSize.height == 4)
						flags |= 2;
					else if (rate.fragmentSize.height == 2)
						flags |= 1;
					for (int y = std::bit_width(rate.fragmentSize.height) - 1; y < 3; ++y)
					{
						for (int x = std::bit_width(rate.fragmentSize.width) - 1; x < 3; ++x)
						{
							if (fragment_sizes[x][y] == 0)
								fragment_sizes[x][y] = flags;
						}
					}
				}
			}
		}
	}

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

int32_t stream_reprojection::shading_rate(int pixels_x, int pixels_y)
{
	int x = 0;
	if (pixels_x >= 2)
		x = 1;
	if (pixels_x >= 4)
		x = 2;
	int y = 0;
	if (pixels_y >= 2)
		y = 1;
	if (pixels_y >= 4)
		y = 2;
	return fragment_sizes[x][y];
}

void stream_reprojection::reproject(vk::raii::CommandBuffer & command_buffer,
                                    const std::array<wivrn::to_headset::foveation_parameter, 2> & foveation,
                                    int destination)
{
	if (destination < 0 || destination >= (int)output_images.size())
		throw std::runtime_error("Invalid destination image index");

	ensure_vertices(std::max(required_vertices(foveation[0]), required_vertices(foveation[1])));

	for (size_t view = 0; view < view_count; ++view)
	{
		auto vertices = get_vertices(view);
		const auto & [px, py] = foveation[view];
		assert(px.size() % 2 == 1);
		assert(py.size() % 2 == 1);
		const int n_ratio_y = (py.size() - 1) / 2;
		const int n_ratio_x = (px.size() - 1) / 2;

		glm::vec2 in(0), out(0); // pixel coordinates
		glm::vec2 in_pixel_size(1. / input_extent.width,
		                        1. / input_extent.height);
		glm::vec2 out_pixel_size(2. / output_extent.width,
		                         2. / output_extent.height);
		for (auto [iy, n_out_y]: utils::enumerate_range(py))
		{
			// number of output pixels per source pixels
			const int ratio_y = std::abs(n_ratio_y - int(iy)) + 1;
			in.x = 0;
			out.x = 0;
			for (auto [ix, n_out_x]: utils::enumerate_range(px))
			{
				const int ratio_x = std::abs(n_ratio_x - int(ix)) + 1;
				*vertices++ = {
				        .position = out * out_pixel_size - glm::vec2(1),
				        .uv = in * in_pixel_size,
				        .shading_rate = shading_rate(ratio_x, ratio_y),
				};
				*vertices++ = {
				        .position = (out + glm::vec2(0, n_out_y * ratio_y)) * out_pixel_size - glm::vec2(1),
				        .uv = (in + glm::vec2(0, n_out_y)) * in_pixel_size,
				        .shading_rate = shading_rate(ratio_x, ratio_y),
				};
				in.x += n_out_x;
				out.x += n_out_x * ratio_x;
			}
			*vertices++ = {
			        .position = out * out_pixel_size - glm::vec2(1),
			        .uv = in * in_pixel_size,
			};
			in.y += n_out_y;
			out.y += n_out_y * ratio_y;
			*vertices++ = {
			        .position = out * out_pixel_size - glm::vec2(1),
			        .uv = in * in_pixel_size,
			};
			*vertices++ = {
			        .position = out * out_pixel_size - glm::vec2(1),
			        .uv = in * in_pixel_size,
			};
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
		                .extent = output_extent,
		        },
		};

		command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
		command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, descriptor_sets[view], {});
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

XrExtent2Di stream_reprojection::defoveated_size(const wivrn::to_headset::foveation_parameter & view) const
{
	return {
	        count_pixels(view.x),
	        count_pixels(view.y),
	};
}
