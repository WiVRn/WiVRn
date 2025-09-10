/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "blitter.h"

#include "application.h"
#include "utils/ranges.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include "vk/specialization_constants.h"
#include <format>
#include <ranges>
#include <spdlog/spdlog.h>

namespace wivrn
{

static const uint32_t num_views = 2;

blitter::blitter(vk::raii::Device & device, size_t view) :
        device(device), view(view)
{
}

void blitter::reset(const to_headset::video_stream_description & desc)
{
	using channels_t = to_headset::video_stream_description::channels_t;
	this->desc = desc;

	current = {};
	target = image_allocation();
	image_view = nullptr;
	rp = nullptr;
	fb = nullptr;
	sampler = nullptr;
	pipelines.clear();
	passthrough_rgb = passthrough_a = -1;

	auto w = desc.width / num_views;
	for (auto [i, item]: utils::enumerate(desc.items))
	{
		if (item.offset_x <= view * w and
		    item.offset_y == 0 and
		    item.offset_x + item.width * item.subsampling >= (view + 1) * w and
		    item.height * item.subsampling >= desc.height)
		{
			spdlog::info("Stream {} is eligible for blit-less path on view {}", i, view);
			switch (item.channels)
			{
				case channels_t::colour:
					passthrough_rgb = i;
					break;
				case channels_t::alpha:
					passthrough_a = i;
					break;
			}
			const bool alpha = item.channels == to_headset::video_stream_description::channels_t::alpha;
			auto & r = alpha ? current.rect_a : current.rect_rgb;
			r = vk::Rect2D{
			        .offset = {
			                .x = int32_t(view * w) - item.offset_x,
			                .y = 0,
			        },
			};
		}
	}

	if (passthrough_rgb != -1 and passthrough_a != -1)
		return;

	target = image_allocation(
	        device,
	        vk::ImageCreateInfo{
	                .imageType = vk::ImageType::e2D,
	                .format = vk::Format::eB8G8R8A8Srgb,
	                .extent = {
	                        .width = w,
	                        .height = desc.height,
	                        .depth = 1,
	                },
	                .mipLevels = 1,
	                .arrayLayers = 1,
	                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
	        },
	        {
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        std::format("blit image {}", view));

	image_view = device.createImageView(vk::ImageViewCreateInfo{
	        .image = vk::Image(target),
	        .viewType = vk::ImageViewType::e2D,
	        .format = target.info().format,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = 1,
	                .layerCount = 1,
	        }});

	// render pass
	{
		vk::AttachmentReference color_ref{
		        .attachment = 0,
		        .layout = vk::ImageLayout::eColorAttachmentOptimal,
		};

		vk::SubpassDescription subpass{
		        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		        .colorAttachmentCount = 1,
		        .pColorAttachments = &color_ref,
		};

		vk::AttachmentDescription color_desc{
		        .format = target.info().format,
		        .loadOp = vk::AttachmentLoadOp::eDontCare,
		        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};

		rp = device.createRenderPass(vk::RenderPassCreateInfo{
		        .attachmentCount = 1,
		        .pAttachments = &color_desc,
		        .subpassCount = 1,
		        .pSubpasses = &subpass,
		});
	}

	fb = device.createFramebuffer(
	        vk::FramebufferCreateInfo{
	                .renderPass = *rp,
	                .attachmentCount = 1,
	                .pAttachments = &*image_view,
	                .width = w,
	                .height = desc.height,
	                .layers = 1,
	        });

	sampler = device.createSampler(vk::SamplerCreateInfo{
	        .magFilter = vk::Filter::eLinear,
	        .minFilter = vk::Filter::eLinear,
	        .addressModeU = vk::SamplerAddressMode::eClampToBorder,
	        .addressModeV = vk::SamplerAddressMode::eClampToBorder,
	        .addressModeW = vk::SamplerAddressMode::eClampToBorder,
	        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
	});

	pipelines.resize(desc.items.size());
	for (auto [i, item]: utils::enumerate(desc.items))
	{
		pipelines[i].used = item.offset_x < (view + 1) * w and
		                    item.offset_x + item.width * item.subsampling > view * w;
	}

	current.rgb = *image_view;
	current.sampler_rgb = *sampler;
	current.rect_rgb = vk::Rect2D{.extent = {w, desc.height}};
	current.layout_rgb = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void blitter::begin(vk::raii::CommandBuffer & cmd)
{
	assert(not desc.items.empty());
	if (*fb == VK_NULL_HANDLE)
	{
		current.rgb = nullptr;
		current.a = nullptr;
		current.sampler_rgb = nullptr;
		current.sampler_a = nullptr;
		return;
	}

	current.rgb = *image_view;
	current.sampler_rgb = *sampler;

	cmd.beginRenderPass(
	        {
	                .renderPass = *rp,
	                .framebuffer = *fb,
	                .renderArea = {
	                        .offset = {0, 0},
	                        .extent = {target.info().extent.width, target.info().extent.height},
	                },
	        },
	        vk::SubpassContents::eInline);
}

void blitter::push_image(vk::raii::CommandBuffer & cmd, uint8_t stream, vk::Sampler sampler, const vk::Extent2D & extent_, vk::ImageView image, vk::ImageLayout layout)
{
	if (stream == passthrough_rgb)
	{
		current.rgb = image;
		current.sampler_rgb = sampler;
		current.layout_rgb = layout;
		current.rect_rgb.extent = vk::Extent2D{
		        extent_.width * desc.items[stream].subsampling,
		        extent_.height * desc.items[stream].subsampling,
		};
		return;
	}
	if (stream == passthrough_a)
	{
		current.a = image;
		current.sampler_a = sampler;
		current.layout_a = layout;
		current.rect_a.extent = vk::Extent2D{
		        extent_.width * desc.items[stream].subsampling,
		        extent_.height * desc.items[stream].subsampling,
		};
		return;
	}
	if (pipelines.empty())
		return;

	assert(stream < pipelines.size());
	auto & p = pipelines[stream];
	if (not p.used)
		return;

	const auto & extent = target.info().extent;
	const auto & description = desc.items[stream];
	if (not *p.pipeline)
	{
		const bool alpha = description.channels == to_headset::video_stream_description::channels_t::alpha;
		vk::DescriptorSetLayoutBinding sampler_layout_binding{
		        .binding = 0,
		        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		        .descriptorCount = 1,
		        .stageFlags = vk::ShaderStageFlagBits::eFragment,
		        .pImmutableSamplers = &sampler,
		};

		p.set_layout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
		        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
		        .bindingCount = 1,
		        .pBindings = &sampler_layout_binding,
		});

		auto vert_constants = make_specialization_constants(
		        float(description.width) / extent_.width,
		        float(description.height) / extent_.height);

		auto frag_constants = make_specialization_constants(
		        VkBool32(alpha));

		// Create graphics pipeline
		vk::raii::ShaderModule vertex_shader = load_shader(device, "stream.vert");
		vk::raii::ShaderModule fragment_shader = load_shader(device, "stream.frag");

		p.layout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
		        .setLayoutCount = 1,
		        .pSetLayouts = &*p.set_layout,
		});

		vk::pipeline_builder pipeline_info{
		        .Stages = {{
		                           .stage = vk::ShaderStageFlagBits::eVertex,
		                           .module = *vertex_shader,
		                           .pName = "main",
		                           .pSpecializationInfo = vert_constants,
		                   },
		                   {
		                           .stage = vk::ShaderStageFlagBits::eFragment,
		                           .module = *fragment_shader,
		                           .pName = "main",
		                           .pSpecializationInfo = frag_constants,
		                   }},
		        .VertexBindingDescriptions = {},
		        .VertexAttributeDescriptions = {},
		        .InputAssemblyState = {{
		                .topology = vk::PrimitiveTopology::eTriangleStrip,
		        }},
		        // With vk::DynamicState::eViewport, vk::DynamicState::eScissor the number of viewports
		        // and scissors is still used, put a vector with one element
		        .Viewports = {{}},
		        .Scissors = {{}},
		        .RasterizationState = {{
		                .polygonMode = vk::PolygonMode::eFill,
		                .lineWidth = 1,
		        }},
		        .MultisampleState = {{
		                .rasterizationSamples = vk::SampleCountFlagBits::e1,
		        }},
		        .ColorBlendAttachments = {
		                {.colorWriteMask = alpha ? vk::ColorComponentFlagBits::eA
		                                         : vk::ColorComponentFlagBits::eA | vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB}},
		        .DynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
		        .layout = *p.layout,
		        .renderPass = *rp,
		        .subpass = 0,
		};

		p.pipeline = device.createGraphicsPipeline(application::get_pipeline_cache(), pipeline_info);
		spdlog::info("Created blit pipeline for stream {}, view {}", stream, view);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *p.pipeline);

	vk::DescriptorImageInfo image_info{
	        .imageView = image,
	        .imageLayout = layout,
	};
	cmd.pushDescriptorSetKHR(
	        vk::PipelineBindPoint::eGraphics,
	        *p.layout,
	        0,
	        vk::WriteDescriptorSet{
	                .dstBinding = 0,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = &image_info,
	        });

	int x0 = description.offset_x - view * extent.width;
	int y0 = description.offset_y;
	int x1 = x0 + description.width * description.subsampling;
	int y1 = y0 + description.height * description.subsampling;

	cmd.setViewport(
	        0,
	        vk::Viewport{
	                .x = (float)x0,
	                .y = (float)y0,
	                .width = float(description.width * description.subsampling),
	                .height = float(description.height * description.subsampling),
	                .minDepth = 0,
	                .maxDepth = 1,
	        });

	x0 = std::clamp<int>(x0, 0, extent.width);
	x1 = std::clamp<int>(x1, 0, extent.width);
	y0 = std::clamp<int>(y0, 0, extent.height);
	y1 = std::clamp<int>(y1, 0, extent.height);

	cmd.setScissor(
	        0,
	        vk::Rect2D{
	                .offset = {.x = x0, .y = y0},
	                .extent = {.width = uint32_t(x1 - x0), .height = uint32_t(y1 - y0)},
	        });
	cmd.draw(3, 1, 0, 0);
}

blitter::output blitter::end(vk::raii::CommandBuffer & cmd)
{
	if (*fb)
		cmd.endRenderPass();
	return current;
}

} // namespace wivrn
