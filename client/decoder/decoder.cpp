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

#ifdef __ANDROID__
#include "decoder/android/android_decoder.h"
#elif WIVRN_USE_V4L2
#include "decoder/v4l2/v4l2_decoder.h"
#else
#include "decoder/ffmpeg/ffmpeg_decoder.h"
#endif
#include "decoder/raw_decoder.h"

wivrn::decoder::~decoder() = default;

std::shared_ptr<wivrn::decoder> wivrn::decoder::make(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & phys_dev,
        uint32_t vk_queue_family_index,
        const wivrn::to_headset::video_stream_description & description,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * acc)
{
	switch (description.codec[stream_index])
	{
		case h264:
		case h265:
		case av1:
#ifdef __ANDROID__
			return std::make_shared<wivrn::android::decoder>(
			        device,
			        phys_dev,
			        description,
			        stream_index,
			        scene,
			        acc);
#elif WIVRN_USE_V4L2
			return std::make_shared<wivrn::v4l2::decoder>(
			        device, phys_dev, vk_queue_family_index, description, stream_index, scene, acc);
#else
			return std::make_shared<wivrn::ffmpeg::decoder>(
			        device, phys_dev, description, stream_index, scene, acc);
#endif
		case raw:
			return std::make_shared<wivrn::raw_decoder>(
			        device,
			        phys_dev,
			        vk_queue_family_index,
			        description,
			        stream_index,
			        scene,
			        acc);
	}
	__builtin_unreachable();
}

static std::vector<wivrn::video_codec_capability> supported_codecs_()
{
	std::vector<wivrn::video_codec> codecs;
#ifdef __ANDROID__
	wivrn::android::decoder::supported_codecs(codecs);
#elif WIVRN_USE_V4L2
	wivrn::v4l2::decoder::supported_codecs(codecs);
#else
	wivrn::ffmpeg::decoder::supported_codecs(codecs);
#endif
	codecs.push_back(wivrn::video_codec::raw);

	std::vector<wivrn::video_codec_capability> res;
	res.reserve(codecs.size());
	for (auto codec: codecs)
		res.push_back({.codec = codec, .supports_10bit = wivrn::decoder::supports_10bit(codec)});
	return res;
}

const std::vector<wivrn::video_codec_capability> & wivrn::decoder::supported_codecs()
{
	static std::vector<wivrn::video_codec_capability> res = supported_codecs_();
	return res;
}

bool wivrn::decoder::supports_10bit(wivrn::video_codec codec)
{
#ifdef __ANDROID__
	return codec == wivrn::video_codec::h265 or codec == wivrn::video_codec::av1;
#elif WIVRN_USE_V4L2
	switch (codec)
	{
		case wivrn::video_codec::h265: {
			static const bool supported = wivrn::v4l2::decoder::supports_10bit(codec);
			return supported;
		}
		case wivrn::video_codec::av1: {
			static const bool supported = wivrn::v4l2::decoder::supports_10bit(codec);
			return supported;
		}
		case wivrn::video_codec::h264:
		case wivrn::video_codec::raw:
			return false;
	}
	__builtin_unreachable();
#else
	// ffmpeg
	return codec == wivrn::video_codec::h265 or codec == wivrn::video_codec::av1;
#endif
}
