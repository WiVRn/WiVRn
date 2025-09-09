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
#include <cassert>
#include <vulkan/vulkan.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libswscale/swscale.h>
}

namespace wivrn::ffmpeg
{
static void free_codec_context(AVCodecContext * ctx)
{
	avcodec_free_context(&ctx);
}

static void free_frame(AVFrame * frame)
{
	av_frame_free(&frame);
}

static AVCodecID codec_id(wivrn::video_codec codec)
{
	using c = wivrn::video_codec;
	switch (codec)
	{
		case c::h264:
			return AV_CODEC_ID_H264;
		case c::h265:
			return AV_CODEC_ID_HEVC;
		case c::av1:
			return AV_CODEC_ID_AV1;
	}
	assert(false);
	__builtin_unreachable();
}

struct decoder::ffmpeg_blit_handle : public wivrn::decoder::blit_handle
{
	int image_index;
	wivrn::ffmpeg::decoder * self;

	ffmpeg_blit_handle(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	        vk::ImageView image_view,
	        vk::Image image,
	        vk::ImageLayout & current_layout,
	        int image_index,
	        wivrn::ffmpeg::decoder * self) :
	        wivrn::decoder::blit_handle{
	                feedback,
	                view_info,
	                image_view,
	                image,
	                current_layout,
	        },
	        image_index(image_index),
	        self(self)
	{}

	~ffmpeg_blit_handle()
	{
		std::unique_lock lock(self->mutex);
		if (image_index < 0)
			return;

		self->free_images.push_back(image_index);
	}
};

decoder::decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        const wivrn::to_headset::video_stream_description::item & description,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * accumulator) :
        wivrn::decoder(description),
        device(device),
        codec(nullptr, free_codec_context),
        sws(nullptr, sws_freeContext),
        weak_scene(scene),
        accumulator(accumulator)
{
	free_images.resize(image_count);

	for (int i = 0; i < image_count; i++)
	{
		free_images[i] = i;

		vk::ImageCreateInfo image_info{
		        .imageType = vk::ImageType::e2D,
		        .format = vk::Format::eA8B8G8R8SrgbPack32,
		        .extent = {
		                .width = description.width,
		                .height = description.height,
		                .depth = 1,
		        },
		        .mipLevels = 1,
		        .arrayLayers = 1,
		        .samples = vk::SampleCountFlagBits::e1,
		        .tiling = vk::ImageTiling::eLinear,
		        .usage = vk::ImageUsageFlagBits::eSampled,
		        .initialLayout = vk::ImageLayout::eUndefined,
		};

		VmaAllocationCreateInfo alloc_info{
		        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

		decoded_images[i].image = image_allocation(device, image_info, alloc_info);

		decoded_images[i].image.map();
		extent_ = vk::Extent2D{description.width, description.height};

		vk::ImageSubresource resource;
		resource.aspectMask = vk::ImageAspectFlagBits::eColor;

		decoded_images[i].layout = decoded_images[i].image->getSubresourceLayout(resource);

		decoded_images[i].image_view = vk::raii::ImageView(
		        device,
		        {
		                .image = (vk::Image)decoded_images[i].image,
		                .viewType = vk::ImageViewType::e2D,
		                .format = image_info.format,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .baseMipLevel = 0,
		                        .levelCount = 1,
		                        .baseArrayLayer = 0,
		                        .layerCount = 1,
		                },
		        });

		decoded_images[i].current_layout = vk::ImageLayout::eUndefined;
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

	rgb_sampler = vk::raii::Sampler(
	        device,
	        {
	                .flags = {},
	                .magFilter = vk::Filter::eLinear,
	                .minFilter = vk::Filter::eLinear,
	                .mipmapMode = vk::SamplerMipmapMode::eNearest,
	                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
	                .unnormalizedCoordinates = false,
	        });
}

void decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	for (const auto & d: data)
		packet.insert(packet.end(), d.begin(), d.end());
	this->frame_index = frame_index;
}

void decoder::frame_completed(const wivrn::from_headset::feedback & feedback, const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
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
	uint8_t * out = decoded_images[index].image.data<uint8_t>();
	res = sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, &out, &dstStride);
	if (res == 0)
		throw std::runtime_error{"sws_scale failed"};

	auto handle = std::make_shared<ffmpeg_blit_handle>(
	        feedback,
	        view_info,
	        *decoded_images[index].image_view,
	        decoded_images[index].image,
	        decoded_images[index].current_layout,
	        index,
	        this);

	if (auto scene = weak_scene.lock())
		scene->push_blit_handle(accumulator, std::move(handle));
}

void decoder::supported_codecs(std::vector<wivrn::video_codec> & res)
{
	res.push_back(wivrn::video_codec::h264);
	res.push_back(wivrn::video_codec::h265);
	res.push_back(wivrn::video_codec::av1);
}

} // namespace wivrn::ffmpeg
