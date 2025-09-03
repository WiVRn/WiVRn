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

#include "decoder.h"

#include "application.h"
#include "scenes/stream.h"

namespace
{
struct pyrowave_blit_handle : public decoder::blit_handle
{
	std::atomic_bool & free;

	pyrowave_blit_handle(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	        vk::ImageView image_view,
	        vk::Image image,
	        vk::ImageLayout & current_layout,
	        vk::Semaphore semaphore,
	        uint64_t & semaphore_val,
	        std::atomic_bool & free) :
	        decoder::blit_handle{feedback, view_info, image_view, image, current_layout, semaphore, &semaphore_val},
	        free(free) {}
	~pyrowave_blit_handle()
	{
		free = true;
	};
};
} // namespace

namespace wivrn
{
pyrowave_decoder::pyrowave_decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        uint32_t vk_queue_family_index,
        const wivrn::to_headset::video_stream_description::item & description,
        float fps,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * accumulator) :
        decoder(description),
        ycbcr_conversion(device, {
                                         .format = vk::Format::eG8B8R83Plane420Unorm,
                                         .ycbcrModel = vk::SamplerYcbcrModelConversion(description.color_model.value_or(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709)),
                                         .ycbcrRange = vk::SamplerYcbcrRange(description.range.value_or(VK_SAMPLER_YCBCR_RANGE_ITU_FULL)),
                                         .chromaFilter = vk::Filter::eNearest,
                                 }),
        sampler_(device, vk::StructureChain{
                                 vk::SamplerCreateInfo{
                                         .magFilter = vk::Filter::eNearest,
                                         .minFilter = vk::Filter::eNearest,
                                         .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                         .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                         .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                         .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                         .maxAnisotropy = 1,
                                 },
                                 vk::SamplerYcbcrConversionInfo{
                                         .conversion = *ycbcr_conversion,
                                 },
                         }
                                 .get()),
        weak_scene(scene),
        accumulator(accumulator),
        dec(physical_device, device, description.width, description.height, PyroWave::ChromaSubsampling::Chroma420, true)
{
	std::array formats = {
	        vk::Format::eR8Unorm,
	        vk::Format::eG8B8R83Plane420Unorm,
	};
	vk::ImageFormatListCreateInfo formats_info{
	        .viewFormatCount = formats.size(),
	        .pViewFormats = formats.data(),
	};
	size_t i = 0;
	for (auto & item: image_pool)
	{
		item.image = image_allocation(
		        device,
		        vk::ImageCreateInfo{
		                .pNext = &formats_info,
		                .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
		                .imageType = vk::ImageType::e2D,
		                .format = formats.back(),
		                .extent = {
		                        .width = description.width,
		                        .height = description.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
		        },
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        std::format("pyrowave decoder {} image {}", stream_index, i));

		vk::SamplerYcbcrConversionInfo conv{
		        .conversion = *ycbcr_conversion,
		};
		vk::ImageViewUsageCreateInfo usage{
		        .pNext = &conv,
		        .usage = vk::ImageUsageFlagBits::eSampled,
		};
		vk::ImageViewCreateInfo view_info{
		        .pNext = &usage,
		        .image = item.image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = formats.back(),
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .layerCount = 1,
		        },
		};
		item.view_full = vk::raii::ImageView(device, view_info);

		usage.pNext = nullptr;
		usage.usage |= vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment;
		view_info.format = formats[0];
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane0;
		item.view_y = vk::raii::ImageView(device, view_info);
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane1;
		item.view_cb = vk::raii::ImageView(device, view_info);
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane2;
		item.view_cr = vk::raii::ImageView(device, view_info);

		item.semaphore = vk::raii::Semaphore(
		        device,
		        vk::StructureChain{
		                vk::SemaphoreCreateInfo{},
		                vk::SemaphoreTypeCreateInfo{
		                        .semaphoreType = vk::SemaphoreType::eTimeline,
		                },
		        }
		                .get());

		++i;
	}

	pending.lock()->input = std::make_unique<PyroWave::DecoderInput>(dec);
	input_acc = std::make_unique<PyroWave::DecoderInput>(dec);

	worker = std::thread([&, vk_queue_family_index] { worker_function(vk_queue_family_index); });
}

pyrowave_decoder::~pyrowave_decoder()
{
	exiting = true;
	pending.lock().notify_all();
	if (worker.joinable())
		worker.join();
}

void pyrowave_decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	for (auto & item: data)
		input_acc->push_data(item);
}

void pyrowave_decoder::frame_completed(
        const from_headset::feedback & feedback,
        const to_headset::video_stream_data_shard::view_info_t & view_info)
{
	{
		auto locked = pending.lock();
		std::swap(input_acc, locked->input);
		locked->ready = true;
		locked->feedback = feedback;
		locked->view_info = view_info;
		locked.notify_all();
	}
	input_acc->clear();
}

pyrowave_decoder::image * pyrowave_decoder::get_free()
{
	for (auto & item: image_pool)
	{
		if (item.free.exchange(false))
			return &item;
	}
	return nullptr;
}

void pyrowave_decoder::worker_function(uint32_t queue_family_index)
{
	auto & device = dec.device;
	vk::raii::CommandPool command_pool(
	        device,
	        vk::CommandPoolCreateInfo{
	                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
	                .queueFamilyIndex = queue_family_index,
	        });
	vk::raii::CommandBuffer cmd_buf(
	        std::move(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	                .commandPool = *command_pool,
	                .commandBufferCount = 1,
	        })[0]));
	vk::raii::Fence fence(device, vk::FenceCreateInfo{});

	vk::raii::QueryPool qp(
	        device, vk::QueryPoolCreateInfo{
	                        .queryType = vk::QueryType::eTimestamp,
	                        .queryCount = 2,
	                });

	from_headset::feedback feedback;
	to_headset::video_stream_data_shard::view_info_t view_info;

	auto input = std::make_unique<PyroWave::DecoderInput>(dec);

	// Record the encode duration for the previous frame, that's the best we can do
	XrDuration last_encode = 0;
	float timestamp_period = application::get_physical_device_properties().limits.timestampPeriod;

	while (not exiting)
	{
		try
		{
			{
				auto locked = pending.lock();
				locked.wait([&]() { return exiting or locked->ready; });
				if (exiting)
					return;

				feedback = locked->feedback;
				view_info = locked->view_info;

				std::swap(locked->input, input);
				locked->input->clear();
				locked->ready = false;
			}
			feedback.received_from_decoder = feedback.sent_to_decoder + last_encode;

			auto item = get_free();
			if (not item)
			{
				spdlog::warn("No image available in pool, discard frame");
				continue;
			}
			std::array views{*item->view_y, *item->view_cb, *item->view_cr};
			auto handle = std::make_shared<pyrowave_blit_handle>(
			        feedback,
			        view_info,
			        *item->view_full,
			        item->image,
			        item->current_layout,
			        *item->semaphore,
			        item->semaphore_val,
			        item->free);
			dec.device.resetFences(*fence);
			cmd_buf.reset();
			cmd_buf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
			cmd_buf.resetQueryPool(*qp, 0, 2);
			cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, *qp, 0);

			if (item->current_layout != vk::ImageLayout::eGeneral)
			{
				item->current_layout = vk::ImageLayout::eGeneral;
				vk::ImageMemoryBarrier barrier{
				        .srcAccessMask = vk::AccessFlagBits::eNone,
				        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
				        .oldLayout = vk::ImageLayout::eUndefined,
				        .newLayout = vk::ImageLayout::eGeneral,
				        .image = item->image,
				        .subresourceRange = {
				                .aspectMask = vk::ImageAspectFlagBits::eColor,
				                .levelCount = 1,
				                .layerCount = 1,
				        },
				};
				cmd_buf.pipelineBarrier(
				        vk::PipelineStageFlagBits::eAllCommands,
				        vk::PipelineStageFlagBits::eTransfer,
				        {},
				        {},
				        {},
				        barrier);
			}
			dec.decode(cmd_buf, *input, views);
			{
				vk::ImageMemoryBarrier barrier{
				        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
				        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
				        .oldLayout = vk::ImageLayout::eGeneral,
				        .newLayout = vk::ImageLayout::eGeneral,
				        .image = item->image,
				        .subresourceRange = {
				                .aspectMask = vk::ImageAspectFlagBits::eColor,
				                .levelCount = 1,
				                .layerCount = 1,
				        },
				};
				cmd_buf.pipelineBarrier(
				        vk::PipelineStageFlagBits::eComputeShader,
				        vk::PipelineStageFlagBits::eFragmentShader,
				        {},
				        {},
				        {},
				        barrier);
			}
			cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, *qp, 1);
			cmd_buf.end();

			application::get_queue().lock()->submit(
			        vk::StructureChain{
			                vk::SubmitInfo{
			                        .commandBufferCount = 1,
			                        .pCommandBuffers = &*cmd_buf,
			                        .signalSemaphoreCount = 1,
			                        .pSignalSemaphores = &*item->semaphore,
			                },
			                vk::TimelineSemaphoreSubmitInfo{
			                        .signalSemaphoreValueCount = 1,
			                        .pSignalSemaphoreValues = &++item->semaphore_val,
			                },
			        }
			                .get(),
			        *fence);

			if (auto scene = weak_scene.lock())
				scene->push_blit_handle(accumulator, std::move(handle));

			auto res = dec.device.waitForFences(*fence, true, UINT64_MAX);
			if (res != vk::Result::eSuccess)
				spdlog::warn("waitForFences failed");

			std::array<uint64_t, 2> times;
			res = (*device).getQueryPoolResults(
			        *qp, 0, 2, sizeof(uint64_t) * 2, times.data(), sizeof(uint64_t), vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
			if (res == vk::Result::eSuccess)
				last_encode = (times[1] - times[0]) * timestamp_period;
		}
		catch (std::exception & e)
		{
			spdlog::warn("pyrowave exception: {}", e.what());
		}
	}
}

std::vector<wivrn::video_codec> pyrowave_decoder::supported_codecs()
{
	return {video_codec::pyrowave};
}
} // namespace wivrn
