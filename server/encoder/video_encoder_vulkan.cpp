/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "vk/vk_helpers.h"

#include "video_encoder_vulkan.h"

#include "encoder/encoder_settings.h"
#include "util/u_logging.h"
#include "utils/scoped_lock.h"
#include "utils/wivrn_vk_bundle.h"
#include <iostream>
#include <stdexcept>

namespace
{
class dpb_state : public wivrn::idr_handler
{
public:
	struct dpb_item
	{
		vk::raii::ImageView image_view;
		vk::VideoPictureResourceInfoKHR resource;
		vk::VideoReferenceSlotInfoKHR & info;
		uint64_t frame_index = -1;
		bool acked = false;
	};

	std::vector<dpb_item> items;
	std::vector<vk::VideoReferenceSlotInfoKHR> infos;
	uint32_t frame_num = 0;
	std::mutex mutex;

	void on_feedback(const wivrn::from_headset::feedback & feedback) override
	{
		if (feedback.sent_to_decoder)
		{
			std::unique_lock lock(mutex);
			for (auto & item: items)
			{
				if (item.frame_index == feedback.frame_index)
				{
					item.acked = true;
					return;
				}
			}
		}
	}

	void reset() override
	{
		std::unique_lock lock(mutex);
		for (auto & item: items)
		{
			item.frame_index = -1;
			item.info.pPictureResource = nullptr;
			item.info.slotIndex = -1;
			item.acked = false;
		}
	}

	bool should_skip(uint64_t frame_id) override
	{
		std::unique_lock lock(mutex);
		bool pending = false; // has any data been sent?
		for (auto & i: items)
		{
			if (i.acked)
				return false;
			if (i.info.slotIndex != -1)
				pending = true;
		}
		return pending;
	}

	std::pair<dpb_item *, dpb_item *> get_ref(uint64_t frame_index)
	{
		// must hold the lock
		auto slot = std::ranges::min_element(
		        items,
		        [](const auto & a, const auto & b) {
			        return a.frame_index + 1 < b.frame_index + 1;
		        });
		slot->info.slotIndex = -1;

		dpb_item * ref_slot = nullptr;
		for (auto & i: items)
		{
			if (i.info.slotIndex == -1 or not i.acked)
				continue;
			if (ref_slot and ref_slot->frame_index > i.frame_index)
				continue;
			ref_slot = &i;
		}

		if (not ref_slot)
		{
			frame_num = 0;
			for (auto & slot: items)
			{
				slot.info.slotIndex = -1;
				slot.info.pPictureResource = nullptr;
				slot.frame_index = -1;
				slot.acked = false;
			}
		}
		slot->acked = false;
		slot->frame_index = frame_index;
		slot->info.pPictureResource = &slot->resource;

		return {ref_slot, &*slot};
	}
};
} // namespace

static uint32_t align(uint32_t value, uint32_t alignment)
{
	if (alignment == 0)
		return value;
	return alignment * (1 + (value - 1) / alignment);
}

static vk::VideoEncodeCapabilitiesKHR patch_capabilities(vk::VideoEncodeCapabilitiesKHR caps)
{
	if (caps.rateControlModes & (vk::VideoEncodeRateControlModeFlagBitsKHR::eCbr | vk::VideoEncodeRateControlModeFlagBitsKHR::eVbr) and caps.maxBitrate == 0)
	{
		U_LOG_W("Invalid encode capabilities, disabling rate control");
		caps.rateControlModes = vk::VideoEncodeRateControlModeFlagBitsKHR::eDefault;
	}
	return caps;
}

vk::VideoFormatPropertiesKHR wivrn::video_encoder_vulkan::select_video_format(
        vk::raii::PhysicalDevice & physical_device,
        const vk::PhysicalDeviceVideoFormatInfoKHR & format_info)
{
	for (const auto & video_fmt_prop: physical_device.getVideoFormatPropertiesKHR(format_info))
	{
		// TODO: do something smart if there is more than one
		return video_fmt_prop;
	}
	throw std::runtime_error("No suitable image format found");
}

wivrn::video_encoder_vulkan::video_encoder_vulkan(
        wivrn_vk_bundle & vk,
        const vk::VideoCapabilitiesKHR & video_caps,
        const vk::VideoEncodeCapabilitiesKHR & in_encode_caps,
        uint8_t stream_idx,
        const encoder_settings & settings) :
        video_encoder(stream_idx, settings, std::make_unique<dpb_state>(), true),
        vk(vk),
        encode_caps(patch_capabilities(in_encode_caps)),
        num_dpb_slots(std::min(video_caps.maxDpbSlots, 16u))
{
	// Initialize Rate control
	U_LOG_D("Supported rate control modes: %s", vk::to_string(encode_caps.rateControlModes).c_str());

	U_LOG_D("video caps:\n"
	        "\t maxDpbSlots: %d\n"
	        "\t maxActiveReferencePictures: %d",
	        video_caps.maxDpbSlots,
	        video_caps.maxActiveReferencePictures);

	if (encode_caps.rateControlModes & (vk::VideoEncodeRateControlModeFlagBitsKHR::eCbr | vk::VideoEncodeRateControlModeFlagBitsKHR::eVbr))
	{
		U_LOG_D("Maximum bitrate: %" PRIu64, encode_caps.maxBitrate / 1'000'000);
		if (encode_caps.maxBitrate < settings.bitrate)
		{
			U_LOG_W("Configured bitrate %" PRIu64 "MB/s is higher than max supported %" PRIu64,
			        settings.bitrate / 1'000'000,
			        encode_caps.maxBitrate / 1'000'000);
		}
	}

	rate_control_layer = vk::VideoEncodeRateControlLayerInfoKHR{
	        .averageBitrate = std::min(settings.bitrate, encode_caps.maxBitrate),
	        .maxBitrate = std::min(2 * settings.bitrate, encode_caps.maxBitrate),
	        .frameRateNumerator = uint32_t(settings.fps * 1'000'000),
	        .frameRateDenominator = 1'000'000,
	};
	rate_control = vk::VideoEncodeRateControlInfoKHR{
	        .layerCount = 1,
	        .pLayers = &rate_control_layer,
	        .virtualBufferSizeInMs = 1'000,
	        .initialVirtualBufferSizeInMs = 500,
	};

	if (encode_caps.rateControlModes & vk::VideoEncodeRateControlModeFlagBitsKHR::eCbr)
	{
		rate_control_layer.maxBitrate = rate_control_layer.averageBitrate;
		rate_control->rateControlMode = vk::VideoEncodeRateControlModeFlagBitsKHR::eCbr;
	}
	else if (encode_caps.rateControlModes & vk::VideoEncodeRateControlModeFlagBitsKHR::eVbr)
	{
		rate_control->rateControlMode = vk::VideoEncodeRateControlModeFlagBitsKHR::eVbr;
	}
	else
	{
		U_LOG_W("No suitable rate control available, reverting to default");
		rate_control.reset();
	}

	constexpr uint32_t cb_min_size = 8;
	uint32_t granWidth =
	        std::max(video_caps.pictureAccessGranularity.width, encode_caps.encodeInputPictureGranularity.width);
	uint32_t granHeight =
	        std::max(video_caps.pictureAccessGranularity.height, encode_caps.encodeInputPictureGranularity.height);

	aligned_extent = vk::Extent3D{
	        .width = align(extent.width, std::max(cb_min_size, granWidth)),
	        .height = align(extent.height, std::max(cb_min_size, granHeight)),
	        .depth = 1,
	};
}

void wivrn::video_encoder_vulkan::init(const vk::VideoCapabilitiesKHR & video_caps,
                                       const vk::VideoProfileInfoKHR & video_profile,
                                       void * video_session_create_next,
                                       void * session_params_next)
{
	vk::VideoProfileListInfoKHR video_profile_list{
	        .profileCount = 1,
	        .pProfiles = &video_profile,
	};

	// Input image
	vk::VideoFormatPropertiesKHR picture_format = select_video_format(
	        vk.physical_device,
	        vk::PhysicalDeviceVideoFormatInfoKHR{
	                .pNext = &video_profile_list,
	                .imageUsage = vk::ImageUsageFlagBits::eVideoEncodeSrcKHR,
	        });

	if (picture_format.format != vk::Format::eG8B8R82Plane420Unorm && picture_format.format != vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16)
		throw std::runtime_error("Unsupported format " +
		                         vk::to_string(picture_format.format) +
		                         " for encoder input image");

	// Decode picture buffer (DPB) images
	vk::VideoFormatPropertiesKHR reference_picture_format = select_video_format(
	        vk.physical_device,
	        vk::PhysicalDeviceVideoFormatInfoKHR{
	                .pNext = &video_profile_list,
	                .imageUsage = vk::ImageUsageFlagBits::eVideoEncodeDpbKHR,
	        });
	{
		// TODO: check format capabilities
		// TODO: use multiple images if array levels are not supported

		vk::ImageCreateInfo img_create_info{
		        .pNext = &video_profile_list,
		        .flags = reference_picture_format.imageCreateFlags,
		        .imageType = reference_picture_format.imageType,
		        .format = reference_picture_format.format,
		        .extent = aligned_extent,
		        .mipLevels = 1,
		        .arrayLayers = num_dpb_slots,
		        .samples = vk::SampleCountFlagBits::e1,
		        .tiling = reference_picture_format.imageTiling,
		        .usage = vk::ImageUsageFlagBits::eVideoEncodeDpbKHR,
		        .sharingMode = vk::SharingMode::eExclusive,
		};

		dpb_image = image_allocation(
		        vk.device,
		        img_create_info,
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "vulkan encoder DPB image");
	}

	// Output buffer
	for (auto & item: slot_data)
	{
		// very conservative bound
		size_t output_buffer_size = extent.width * extent.height * 3;
		output_buffer_size = align(output_buffer_size, video_caps.minBitstreamBufferSizeAlignment);
		item.output_buffer = buffer_allocation(
		        vk.device,
		        {
		                .pNext = &video_profile_list,
		                .size = output_buffer_size,
		                .usage = vk::BufferUsageFlagBits::eVideoEncodeDstKHR | vk::BufferUsageFlagBits::eTransferSrc,
		                .sharingMode = vk::SharingMode::eExclusive,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "vulkan encode output buffer");

		if (not(item.output_buffer.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			U_LOG_D("Using staging buffer for vulkan encode output");
			item.host_buffer = buffer_allocation(
			        vk.device,
			        {
			                .size = output_buffer_size,
			                .usage = vk::BufferUsageFlagBits::eTransferDst,
			                .sharingMode = vk::SharingMode::eExclusive,
			        },
			        {
			                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			        },
			        "vulkan encode host buffer");
		}
	}

	// video session
	{
		vk::ExtensionProperties std_header_version = this->std_header_version();

		video_session =
		        vk.device.createVideoSessionKHR(vk::VideoSessionCreateInfoKHR{
		                .pNext = video_session_create_next,
		                .queueFamilyIndex = vk.encode_queue_family_index,
		                //.flags = vk::VideoSessionCreateFlagBitsKHR::eAllowEncodeParameterOptimizations,
		                .pVideoProfile = &video_profile,
		                .pictureFormat = picture_format.format,
		                .maxCodedExtent = {aligned_extent.width, aligned_extent.height},
		                .referencePictureFormat = reference_picture_format.format,
		                .maxDpbSlots = num_dpb_slots,
		                .maxActiveReferencePictures = 1, // h265 code assumes only 1 reference
		                .pStdHeaderVersion = &std_header_version,
		        });

		auto video_req = video_session.getMemoryRequirements();
		// FIXME: allocating on a single device memory seems to fail
		std::vector<vk::BindVideoSessionMemoryInfoKHR> video_session_bind;
		for (const auto & req: video_req)
		{
			vk::MemoryAllocateInfo alloc_info{
			        .allocationSize = req.memoryRequirements.size,
			        .memoryTypeIndex = vk.get_memory_type(req.memoryRequirements.memoryTypeBits, {}),
			};

			const auto & mem_item = mem.emplace_back(vk.device.allocateMemory(alloc_info));
			video_session_bind.push_back({
			        .memoryBindIndex = req.memoryBindIndex,
			        .memory = *mem_item,
			        .memoryOffset = 0,
			        .memorySize = alloc_info.allocationSize,
			});
		}
		video_session.bindMemory(video_session_bind);
	}

	// input image view
	image_view_template_next = {
	        .usage = vk::ImageUsageFlagBits::eVideoEncodeSrcKHR,
	};
	image_view_template = {
	        .pNext = &image_view_template_next,
	        .viewType = vk::ImageViewType::e2D,
	        .format = picture_format.format,
	        .components = picture_format.componentMapping,
	        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                             .baseMipLevel = 0,
	                             .levelCount = 1,
	                             .baseArrayLayer = stream_idx,
	                             .layerCount = 1},
	};

	if (not vk.vk.features.video_maintenance_1)
	{
		image_view_template.subresourceRange.baseArrayLayer = 0;
		for (size_t i = 0; i < num_slots; ++i)
		{
			slot_data[i].tmp_image = image_allocation(
			        vk.device, {
			                           .pNext = &video_profile_list,
			                           .imageType = vk::ImageType::e2D,
			                           .format = picture_format.format,
			                           .extent = {
			                                   .width = extent.width,
			                                   .height = extent.height,
			                                   .depth = 1,
			                           },
			                           .mipLevels = 1,
			                           .arrayLayers = 1,
			                           .samples = vk::SampleCountFlagBits::e1,
			                           .tiling = vk::ImageTiling::eOptimal,
			                           .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eVideoEncodeSrcKHR,
			                           .sharingMode = vk::SharingMode::eExclusive,
			                   },
			        {
			                .usage = VMA_MEMORY_USAGE_AUTO,
			        },
			        "vulkan encoder temporary image");
			image_view_template.image = vk::Image(slot_data[i].tmp_image);
			slot_data[i].view = vk.device.createImageView(image_view_template);
			vk.name(slot_data[i].view, "vulkan encoder temporary image view");
		}
	}

	auto & dpb = (dpb_state &)*idr;

	// DPB slot info
	{
		auto std_slots = setup_slot_info(num_dpb_slots);
		assert(std_slots.size() == num_dpb_slots);
		for (size_t i = 0; i < num_dpb_slots; ++i)
		{
			dpb.infos.push_back({
			        .pNext = std_slots[i],
			        .slotIndex = -1,
			        .pPictureResource = nullptr,
			});
		}
	}

	// DPB image views
	{
		vk::ImageViewCreateInfo img_view_create_info{
		        .image = dpb_image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = reference_picture_format.format,
		        .components = reference_picture_format.componentMapping,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .layerCount = 1},
		};
		for (size_t i = 0; i < num_dpb_slots; ++i)
		{
			img_view_create_info.subresourceRange.baseArrayLayer = i;
			vk::raii::ImageView v(vk.device, img_view_create_info);
			vk::ImageView v1(*v);
			vk.name(v, "vulkan encoder dpb view");
			dpb.items.push_back({
			        .image_view = std::move(v),
			        .resource = {
			                .codedExtent = extent,
			                .imageViewBinding = v1,
			        },
			        .info = dpb.infos[i],
			});
		}
	}

	// video session parameters
	{
		video_session_parameters = vk.device.createVideoSessionParametersKHR({
		        .pNext = session_params_next,
		        .videoSession = *video_session,
		});
	}

	// fence, semaphore
	for (auto & item: slot_data)
	{
		item.fence = vk.device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
		item.wait_sem = vk.device.createSemaphore({});
		vk.name(item.fence, "vulkan encoder fence");
		vk.name(item.fence, "vulkan encoder semaphore");
	}

	// query pool
	{
		vk::StructureChain query_pool_create = {
		        vk::QueryPoolCreateInfo{
		                .queryType = vk::QueryType::eVideoEncodeFeedbackKHR,
		                .queryCount = num_slots,

		        },
		        vk::QueryPoolVideoEncodeFeedbackCreateInfoKHR{
		                .pNext = &video_profile,
		                .encodeFeedbackFlags =
		                        vk::VideoEncodeFeedbackFlagBitsKHR::eBitstreamBytesWritten,
		        },
		};

		query_pool = vk.device.createQueryPool(query_pool_create.get());
		vk.name(query_pool, "vulkan encoder query pool");
	}

	// command pools
	{
		video_command_pool = vk.device.createCommandPool(
		        vk::CommandPoolCreateInfo{
		                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		                .queueFamilyIndex = vk.encode_queue_family_index,
		        });
		vk.name(query_pool, "vulkan encoder video command pool");

		auto command_buffers = vk.device.allocateCommandBuffers(
		        {.commandPool = *video_command_pool,
		         .commandBufferCount = num_slots});

		for (size_t i = 0; i < num_slots; ++i)
		{
			slot_data[i].video_cmd_buf = std::move(command_buffers[i]);
			vk.name(slot_data[i].video_cmd_buf, "vulkan encoder video command buffer");
		}

		auto properties = vk.physical_device.getQueueFamilyProperties().at(vk.encode_queue_family_index);
		if (not(properties.queueFlags & vk::QueueFlagBits::eTransfer))
		{
			transfer_command_pool = vk.device.createCommandPool(
			        vk::CommandPoolCreateInfo{
			                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			                .queueFamilyIndex = vk.queue_family_index,
			        });
			vk.name(transfer_command_pool, "vulkan encoder transfer command pool");

			auto command_buffers = vk.device.allocateCommandBuffers(
			        {.commandPool = *transfer_command_pool,
			         .commandBufferCount = num_slots});
			for (size_t i = 0; i < num_slots; ++i)
			{
				slot_data[i].sem = vk.device.createSemaphore({});
				slot_data[i].transfer_cmd_buf = std::move(command_buffers[i]);
				vk.name(slot_data[i].sem, "vulkan encoder transfer semaphore");
				vk.name(slot_data[i].transfer_cmd_buf, "vulkan encoder transfer command buffer");
			}
		}
	}
}

wivrn::video_encoder_vulkan::~video_encoder_vulkan()
{
}

std::vector<uint8_t> wivrn::video_encoder_vulkan::get_encoded_parameters(void * next)
{
	auto [feedback, encoded] = vk.device.getEncodedVideoSessionParametersKHR({
	        .pNext = next,
	        .videoSessionParameters = *video_session_parameters,
	});
	return encoded;
}

std::optional<wivrn::video_encoder::data> wivrn::video_encoder_vulkan::encode(uint8_t encode_slot, uint64_t frame_index)
{
	auto & slot_item = slot_data[encode_slot];
	if (slot_item.idr)
		send_idr_data();

	if (auto res = vk.device.waitForFences(*slot_item.fence, true, 1'000'000'000);
	    res != vk::Result::eSuccess)
	{
		throw std::runtime_error("wait for fences: " + vk::to_string(res));
	}

	auto [res, size] = query_pool.getResult<uint32_t>(encode_slot, 1, 0, vk::QueryResultFlagBits::eWait);
	if (res != vk::Result::eSuccess)
	{
		std::cerr << "device.getQueryPoolResults: " << vk::to_string(res) << std::endl;
	}

	// We don't copy the whole buffer, but an estimate of how much we'll need
	// If that wasn't enough, we have to issue a second copy command for the rest
	if (size > slot_item.copy_size)
	{
		U_LOG_D("additional copy needed: %ld", size - slot_item.copy_size);
		const bool main_queue = *slot_item.transfer_cmd_buf;
		// The encoded image is larger than expected, we need to copy some more data
		vk::raii::CommandBuffer & cmd_buf = main_queue ? slot_item.transfer_cmd_buf : slot_item.video_cmd_buf;

		cmd_buf.reset();
		cmd_buf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

		cmd_buf.copyBuffer(
		        slot_item.output_buffer,
		        slot_item.host_buffer,
		        vk::BufferCopy{
		                .srcOffset = slot_item.copy_size,
		                .dstOffset = slot_item.copy_size,
		                .size = size - slot_item.copy_size,
		        });

		cmd_buf.end();

		vk.device.resetFences(*slot_item.fence);

		{
			wivrn::scoped_lock lock(main_queue ? vk.vk.main_queue->mutex : vk.vk.encode_queue->mutex);
			auto & queue = main_queue ? vk.queue : vk.encode_queue;
			queue.submit(vk::SubmitInfo{
			                     .commandBufferCount = 1,
			                     .pCommandBuffers = &*cmd_buf,
			             },
			             *slot_item.fence);
		}
		if (auto res = vk.device.waitForFences(*slot_item.fence, true, 1'000'000'000);
		    res != vk::Result::eSuccess)
		{
			throw std::runtime_error("wait for fences: " + vk::to_string(res));
		}
	}

	void * mapped = slot_item.host_buffer ? slot_item.host_buffer.map() : slot_item.output_buffer.map();

	return data{
	        .encoder = this,
	        .span = std::span(((uint8_t *)mapped), size),
	        .prefer_control = slot_item.idr,
	};
}

std::pair<bool, vk::Semaphore> wivrn::video_encoder_vulkan::present_image(vk::Image y_cbcr, bool transferred, vk::raii::CommandBuffer & cmd_buf, uint8_t encode_slot, uint64_t frame_index)
{
	auto & slot_item = slot_data[encode_slot];
	auto & video_cmd_buf = slot_item.video_cmd_buf;
	video_cmd_buf.reset();
	video_cmd_buf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	// If we encode from top left corner, encode from the source image directly
	bool encode_direct = not slot_item.tmp_image;

	vk::ImageView image_view;

	if (encode_direct)
	{
		if (not transferred)
		{
			vk::ImageMemoryBarrier2 video_barrier{
			        .srcStageMask = vk::PipelineStageFlagBits2KHR::eTransfer,
			        .dstStageMask = vk::PipelineStageFlagBits2KHR::eVideoEncodeKHR,
			        .dstAccessMask = vk::AccessFlagBits2::eVideoEncodeReadKHR,
			        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
			        .newLayout = vk::ImageLayout::eVideoEncodeSrcKHR,
			        .srcQueueFamilyIndex = vk.queue_family_index,
			        .dstQueueFamilyIndex = vk.encode_queue_family_index,
			        .image = y_cbcr,
			        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
			                             .baseMipLevel = 0,
			                             .levelCount = 1,
			                             .baseArrayLayer = 0,
			                             .layerCount = vk::RemainingArrayLayers},
			};
			video_cmd_buf.pipelineBarrier2({
			        .imageMemoryBarrierCount = 1,
			        .pImageMemoryBarriers = &video_barrier,
			});
		}

		auto it = image_views.find(VkImage(y_cbcr));
		if (it != image_views.end())
			image_view = *it->second;
		else
		{
			image_view_template.image = y_cbcr;
			image_view = *image_views.emplace(y_cbcr, vk.device.createImageView(image_view_template)).first->second;
		}
	}
	else
	{
		vk::ImageMemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eNone,
		        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
		        .oldLayout = vk::ImageLayout::eUndefined,
		        .newLayout = vk::ImageLayout::eTransferDstOptimal,
		        .image = slot_item.tmp_image,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		};

		cmd_buf.pipelineBarrier(
		        vk::PipelineStageFlagBits::eNone,
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::DependencyFlags{},
		        {},
		        {},
		        barrier);

		cmd_buf.copyImage(
		        y_cbcr,
		        vk::ImageLayout::eTransferSrcOptimal,
		        slot_item.tmp_image,
		        vk::ImageLayout::eTransferDstOptimal,
		        {
		                vk::ImageCopy{
		                        .srcSubresource = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                                .baseArrayLayer = stream_idx,
		                                .layerCount = 1,
		                        },
		                        .dstSubresource = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                                .baseArrayLayer = 0,
		                                .layerCount = 1,
		                        },
		                        .extent = {
		                                .width = extent.width,
		                                .height = extent.height,
		                                .depth = 1,
		                        },
		                },
		                vk::ImageCopy{
		                        .srcSubresource = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                                .baseArrayLayer = stream_idx,
		                                .layerCount = 1,
		                        },
		                        .dstSubresource = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                                .baseArrayLayer = 0,
		                                .layerCount = 1,
		                        },
		                        .extent = {
		                                .width = extent.width / 2,
		                                .height = extent.height / 2,
		                                .depth = 1,
		                        },
		                },
		        });

		barrier.srcAccessMask = barrier.dstAccessMask;
		barrier.dstAccessMask = vk::AccessFlagBits::eNone;
		barrier.srcQueueFamilyIndex = vk.queue_family_index;
		barrier.dstQueueFamilyIndex = vk.encode_queue_family_index;
		barrier.oldLayout = barrier.newLayout;
		barrier.newLayout = vk::ImageLayout::eVideoEncodeSrcKHR;

		cmd_buf.pipelineBarrier(
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::PipelineStageFlagBits::eNone,
		        vk::DependencyFlags{},
		        {},
		        {},
		        barrier);

		vk::ImageMemoryBarrier2 video_barrier{
		        .srcStageMask = vk::PipelineStageFlagBits2KHR::eTransfer,
		        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
		        .dstStageMask = vk::PipelineStageFlagBits2KHR::eVideoEncodeKHR,
		        .dstAccessMask = vk::AccessFlagBits2::eVideoEncodeReadKHR,
		        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
		        .newLayout = vk::ImageLayout::eVideoEncodeSrcKHR,
		        .srcQueueFamilyIndex = vk.queue_family_index,
		        .dstQueueFamilyIndex = vk.encode_queue_family_index,
		        .image = slot_item.tmp_image,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = 1},
		};
		video_cmd_buf.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &video_barrier,
		});
		image_view = slot_item.view;
	}

	video_cmd_buf.resetQueryPool(*query_pool, encode_slot, 1);

	auto & dpb = (dpb_state &)*idr;
	std::unique_lock lock(dpb.mutex);
	auto [ref_slot, slot] = dpb.get_ref(frame_index);
	slot_item.idr = ref_slot == nullptr;

	vk::VideoReferenceSlotInfoKHR init_refs[2] = {};
	init_refs[0] = slot->info;
	init_refs[0].slotIndex = -1;
	init_refs[0].pPictureResource = &slot->resource;

	if (ref_slot)
	{
		init_refs[1] = ref_slot->info;
		init_refs[1].slotIndex = ref_slot->info.slotIndex;
		init_refs[1].pPictureResource = &ref_slot->resource;
	}

	video_cmd_buf.beginVideoCodingKHR({
	        .pNext = (session_initialized and rate_control) ? &rate_control.value() : nullptr,
	        .videoSession = *video_session,
	        .videoSessionParameters = *video_session_parameters,
	        .referenceSlotCount = ref_slot ? 2u : 1u,
	        .pReferenceSlots = init_refs,
	});

	size_t slot_index = std::distance(dpb.items.data(), slot);
	slot->info.slotIndex = slot_index;

	if (not session_initialized)
	{
		// Initialize encoding session and rate control
		vk::VideoCodingControlInfoKHR control_info{
		        .flags = vk::VideoCodingControlFlagBitsKHR::eReset,
		};
		if (rate_control)
		{
			control_info.flags |= vk::VideoCodingControlFlagBitsKHR::eEncodeRateControl;
			control_info.pNext = &rate_control.value();
		}
		video_cmd_buf.controlVideoCodingKHR(control_info);

		// Set decoded picture buffer to correct layout
		vk::ImageMemoryBarrier2 dpb_barrier{
		        .srcStageMask = vk::PipelineStageFlagBits2KHR::eNone,
		        .srcAccessMask = vk::AccessFlagBits2::eNone,
		        .dstStageMask = vk::PipelineStageFlagBits2KHR::eVideoEncodeKHR,
		        .dstAccessMask = vk::AccessFlagBits2::eVideoEncodeReadKHR | vk::AccessFlagBits2::eVideoEncodeWriteKHR,
		        .oldLayout = vk::ImageLayout::eUndefined,
		        .newLayout = vk::ImageLayout::eVideoEncodeDpbKHR,
		        .image = dpb_image,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = vk::RemainingArrayLayers},
		};
		video_cmd_buf.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &dpb_barrier,
		});
		session_initialized = true;
	}

	bool update_rate_control = false;
	if (auto bitrate = pending_bitrate.exchange(0); bitrate and rate_control)
	{
		rate_control_layer.averageBitrate = std::min<uint64_t>(bitrate, encode_caps.maxBitrate);
		switch (rate_control->rateControlMode)
		{
			case vk::VideoEncodeRateControlModeFlagBitsKHR::eCbr:
				rate_control_layer.maxBitrate = rate_control_layer.averageBitrate;
				break;
			case vk::VideoEncodeRateControlModeFlagBitsKHR::eVbr:
				rate_control_layer.maxBitrate = std::min(2 * rate_control_layer.averageBitrate, encode_caps.maxBitrate);
				break;
			default:
				break;
		}
		update_rate_control = true;
	}
	if (auto framerate = pending_framerate.exchange(0); framerate and rate_control)
	{
		rate_control_layer.frameRateNumerator = uint32_t(framerate * 1'000'000);
		rate_control_layer.frameRateDenominator = 1'000'000;
		update_rate_control = true;
	}
	if (update_rate_control)
		video_cmd_buf.controlVideoCodingKHR(
		        vk::VideoCodingControlInfoKHR{
		                .pNext = &rate_control.value(),
		                .flags = vk::VideoCodingControlFlagBitsKHR::eEncodeRateControl,
		        });

	if (rate_control)
		slot_item.copy_size = (rate_control_layer.maxBitrate * rate_control_layer.frameRateDenominator) / rate_control_layer.frameRateNumerator;
	else
		slot_item.copy_size = slot_item.output_buffer.info().size;

	vk::VideoEncodeInfoKHR encode_info{
	        .pNext = encode_info_next(dpb.frame_num, slot_index, ref_slot ? std::make_optional(ref_slot->info.slotIndex) : std::nullopt),
	        .dstBuffer = slot_item.output_buffer,
	        .dstBufferOffset = 0,
	        .dstBufferRange = slot_item.output_buffer.info().size,
	        .srcPictureResource = {
	                .codedOffset = {0, 0},
	                .codedExtent = {aligned_extent.width, aligned_extent.height},
	                .baseArrayLayer = 0,
	                .imageViewBinding = image_view,
	        },
	        .pSetupReferenceSlot = &slot->info,
	};

	if (ref_slot)
		encode_info.setReferenceSlots(ref_slot->info);

	video_cmd_buf.beginQuery(*query_pool, encode_slot, {});
	video_cmd_buf.encodeVideoKHR(encode_info);
	video_cmd_buf.endQuery(*query_pool, encode_slot);
	video_cmd_buf.endVideoCodingKHR(vk::VideoEndCodingInfoKHR{});

	// When output buffer is not visible, we have to issue a transfer operation
	if (slot_item.host_buffer)
	{
		vk::BufferMemoryBarrier2 barrier{
		        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
		        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
		        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
		        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
		        .srcQueueFamilyIndex = vk.encode_queue_family_index,
		        .dstQueueFamilyIndex = vk.encode_queue_family_index,
		        .buffer = slot_item.output_buffer,
		        .size = vk::WholeSize,
		};

		vk::raii::CommandBuffer * transfer_cmd_buf;
		if (*slot_item.transfer_cmd_buf)
		{
			// We need to transfer the buffer on the main queue
			barrier.dstQueueFamilyIndex = vk.queue_family_index;
			transfer_cmd_buf = &slot_item.transfer_cmd_buf;

			transfer_cmd_buf->reset();
			transfer_cmd_buf->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
			transfer_cmd_buf->pipelineBarrier2({
			        .bufferMemoryBarrierCount = 1,
			        .pBufferMemoryBarriers = &barrier,
			});
		}
		else
			transfer_cmd_buf = &video_cmd_buf;

		video_cmd_buf.pipelineBarrier2({
		        .bufferMemoryBarrierCount = 1,
		        .pBufferMemoryBarriers = &barrier,
		});

		transfer_cmd_buf->copyBuffer(
		        slot_item.output_buffer,
		        slot_item.host_buffer,
		        vk::BufferCopy{
		                .size = slot_item.copy_size,
		        });

		if (*slot_item.transfer_cmd_buf)
			transfer_cmd_buf->end();
	}

	video_cmd_buf.end();

	++dpb.frame_num;

	// If we encode directly from the source, request a transition to video queue
	return {encode_direct, slot_item.wait_sem};
}

void wivrn::video_encoder_vulkan::post_submit(uint8_t slot)
{
	auto & slot_item = slot_data[slot];
	const bool need_transfer = *slot_item.transfer_cmd_buf and slot_item.host_buffer;
	// Issue encode command, and if necessary transfer command
	vk.device.resetFences(*slot_item.fence);
	vk::SemaphoreSubmitInfo wait_sem_info{
	        .semaphore = *slot_item.wait_sem,
	        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
	};
	vk::SemaphoreSubmitInfo sem_info{
	        .semaphore = slot_item.sem,
	        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
	};
	vk::CommandBufferSubmitInfo cmd_info{
	        .commandBuffer = slot_item.video_cmd_buf,
	};

	{
		scoped_lock lock(vk.vk.encode_queue->mutex);
		vk.encode_queue.submit2(vk::SubmitInfo2{
		                                .waitSemaphoreInfoCount = 1,
		                                .pWaitSemaphoreInfos = &wait_sem_info,
		                                .commandBufferInfoCount = 1,
		                                .pCommandBufferInfos = &cmd_info,
		                                .signalSemaphoreInfoCount = need_transfer ? 1u : 0,
		                                .pSignalSemaphoreInfos = &sem_info,
		                        },
		                        need_transfer ? nullptr : *slot_item.fence);
	}
	if (need_transfer)
	{
		// caller already holds the lock
		vk::CommandBufferSubmitInfo cmd_info{
		        .commandBuffer = slot_item.transfer_cmd_buf,
		};
		vk.queue.submit2(vk::SubmitInfo2{
		                         .waitSemaphoreInfoCount = 1,
		                         .pWaitSemaphoreInfos = &sem_info,
		                         .commandBufferInfoCount = 1,
		                         .pCommandBufferInfos = &cmd_info,
		                 },
		                 *slot_item.fence);
	}
}
