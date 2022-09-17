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

#include "ffmpeg_decoder.h"

#include "scenes/stream.h"
#include "spdlog/spdlog.h"
#include "utils/check.h"
#include <cassert>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg
{
static void free_codec_context(AVCodecContext * ctx)
{
	avcodec_free_context(&ctx);
}

static void free_frame(AVFrame * frame)
{
	av_frame_free(&frame);
}

static AVCodecID codec_id(xrt::drivers::wivrn::video_codec codec)
{
	using c = xrt::drivers::wivrn::video_codec;
	switch (codec)
	{
		case c::h264:
			return AV_CODEC_ID_H264;
		case c::h265:
			return AV_CODEC_ID_HEVC;
	}
	assert(false);
	__builtin_unreachable();
}

decoder::blit_handle::~blit_handle()
{
	std::lock_guard lock(self->mutex);
	if (image_index < 0)
		return;

	self->free_images.push_back(image_index);
}

decoder::decoder(VkDevice device, VkPhysicalDevice physical_device, const xrt::drivers::wivrn::to_headset::video_stream_description::item & description, std::weak_ptr<scenes::stream> scene, shard_accumulator * accumulator) :
        device(device), description(description), codec(nullptr, free_codec_context), sws(nullptr, sws_freeContext), weak_scene(scene), accumulator(accumulator)
{
	free_images.resize(image_count);

	for (int i = 0; i < image_count; i++)
	{
		free_images[i] = i;

		VkImageCreateInfo image_info{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
		image_info.extent.width = description.width;
		image_info.extent.height = description.height;
		image_info.extent.depth = 1;
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.tiling = VK_IMAGE_TILING_LINEAR;
		image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.queueFamilyIndexCount = 0;
		image_info.pQueueFamilyIndices = NULL;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		decoded_images[i].image = vk::image(device, image_info);
		decoded_images[i].memory = vk::device_memory(device, physical_device, decoded_images[i].image, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		decoded_images[i].memory.map_memory();

		VkImageSubresource resource{};
		resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		vkGetImageSubresourceLayout(device, decoded_images[i].image, &resource, &decoded_images[i].layout);
	}

	auto avcodec = avcodec_find_decoder(codec_id(description.codec));
	if (avcodec == nullptr)
	{
		throw std::runtime_error{"avcodec_find_decoder failed"};
	}

	codec.reset(avcodec_alloc_context3(avcodec));

	int ret = avcodec_open2(codec.get(), avcodec, nullptr);
	if (ret < 0)
		throw std::runtime_error{"avcodec_open2 failed"};
}

void decoder::set_blit_targets(std::vector<blit_target> targets, VkFormat format)
{
	blit_targets = std::move(targets);
}

void decoder::blit(VkCommandBuffer command_buffer, blit_handle & handle, std::span<int> blit_indices)
{
	std::unique_lock lock(mutex);

	// Transition layout to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	// TODO: transition only if needed
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = handle.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	for (int blit_index: blit_indices)
	{
		uint32_t left = blit_targets[blit_index].offset.x;
		uint32_t right = left + blit_targets[blit_index].extent.width;

		uint32_t width = blit_targets[blit_index].extent.width;
		uint32_t height = blit_targets[blit_index].extent.height;

		// check for intersection
		if (description.offset_x >= right or description.offset_x + description.width <= left)
			continue;

		VkImageBlit blit{};

		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[0].x = std::max<int32_t>(0, left - description.offset_x);
		blit.srcOffsets[0].y = 0;
		blit.srcOffsets[1].x = std::min<int32_t>(description.width, right - description.offset_x);
		blit.srcOffsets[1].y = std::min<int32_t>(description.height, height - description.offset_y);
		blit.srcOffsets[1].z = 1;

		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.dstOffsets[0].x = std::max<int32_t>(0, description.offset_x - left);
		blit.dstOffsets[0].y = description.offset_y;
		blit.dstOffsets[1].x = std::min<int32_t>(width, description.offset_x + description.width - left);
		blit.dstOffsets[1].y = std::min<int32_t>(height, description.offset_y + description.height);
		blit.dstOffsets[1].z = 1;
		vkCmdBlitImage(command_buffer, handle.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blit_targets[blit_index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
	}
}

void decoder::push_data(std::span<uint8_t> data, uint64_t frame_index, bool partial)
{
	packet.insert(packet.end(), data.begin(), data.end());
	this->frame_index = frame_index;
}

void decoder::frame_completed(const xrt::drivers::wivrn::from_headset::feedback & feedback, const xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
	spdlog::trace("ffmpeg decoder:frame_completed {}", frame_index);
	AVPacket packet{};
	packet.pts = AV_NOPTS_VALUE;
	packet.dts = AV_NOPTS_VALUE;
	packet.data = this->packet.data();
	packet.size = this->packet.size();
	packet.pos = -1;

	int res = AVERROR(EAGAIN);
	while (res == AVERROR(EAGAIN))
	{
		res = avcodec_send_packet(codec.get(), &packet);
		if (res == AVERROR(EAGAIN))
		{
			spdlog::warn("EAGAIN in avcodec_send_packet");
			frame_completed(feedback, view_info);
		}
	}
	if (res < 0)
		throw std::runtime_error{"avcodec_send_packet failed"};

	std::unique_ptr<AVFrame, void (*)(AVFrame *)> frame(av_frame_alloc(), free_frame);
	res = avcodec_receive_frame(codec.get(), frame.get());
	if (res == AVERROR(EAGAIN))
		return;
	if (res != 0)
		throw std::runtime_error{"avcodec_receive_frame failed"};

	this->packet.clear();

	if (!sws)
	{
		sws.reset(sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, description.width, description.height, AV_PIX_FMT_RGB0, SWS_BILINEAR, nullptr, nullptr, nullptr));
	}

	std::unique_lock lock(mutex);
	if (free_images.empty())
	{
		spdlog::warn("No free image");
		return;
	}

	int index = free_images.back();
	free_images.pop_back();
	lock.unlock();

	decoded_images[index].frame_index = frame_index;
	int dstStride = decoded_images[index].layout.rowPitch;
	uint8_t * out = (uint8_t *)decoded_images[index].memory.data();
	res = sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, &out, &dstStride);
	if (res == 0)
		throw std::runtime_error{"sws_scale failed"};

	auto handle = std::make_shared<decoder::blit_handle>();
	handle->feedback = feedback;
	handle->view_info = view_info;
	handle->image = decoded_images[index].image;
	handle->image_index = index;
	handle->self = this;

	if (auto scene = weak_scene.lock())
		scene->push_blit_handle(accumulator, std::move(handle));
}

} // namespace ffmpeg
