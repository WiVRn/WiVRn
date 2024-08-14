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

#include "encoder_settings.h"
#include "driver/configuration.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"
#include "video_encoder.h"

#include <cmath>
#include <magic_enum.hpp>
#include <string>
#include <vulkan/vulkan.h>

#include "wivrn_config.h"

#ifdef WIVRN_USE_NVENC
#include "video_encoder_nvenc.h"
#endif
#ifdef WIVRN_USE_VAAPI
#include "ffmpeg/video_encoder_va.h"
#endif

using namespace xrt::drivers::wivrn;

// TODO: size independent bitrate
static const uint64_t default_bitrate = 50'000'000;

// #define WIVRN_SPLIT_ENCODERS 1

static bool is_nvidia(vk::PhysicalDevice physical_device)
{
	auto props = physical_device.getProperties();
	return props.vendorID == 0x10DE;
}

static void split_bitrate(std::vector<xrt::drivers::wivrn::encoder_settings> & encoders, uint64_t bitrate)
{
	double total_weight = 0;
	for (auto & encoder: encoders)
	{
		double w = encoder.width * encoder.height;
		switch (encoder.codec)
		{
			case xrt::drivers::wivrn::h264:
				w *= 2;
				break;
			case xrt::drivers::wivrn::h265:
			case xrt::drivers::wivrn::av1:
				break;
		}
		encoder.bitrate = w;
		total_weight += w;
	}

	for (auto & encoder: encoders)
	{
		encoder.bitrate = encoder.bitrate * bitrate / total_weight;
	}
}

void print_encoders(const std::vector<xrt::drivers::wivrn::encoder_settings> & encoders)
{
	int group = -1;
	for (auto & encoder: encoders)
	{
		if (encoder.group != group)
		{
			group = encoder.group;
			U_LOG_I("Group %d", group);
		}
		std::string codec(magic_enum::enum_name(encoder.codec));
		U_LOG_I("\t%s (%s)", encoder.encoder_name.c_str(), codec.c_str());
		U_LOG_I("\tsize:%dx%d offset:%dx%d",
		        encoder.width,
		        encoder.height,
		        encoder.offset_x,
		        encoder.offset_y);
		U_LOG_I("\tbitrate: %ldMbit/s", encoder.bitrate / 1'000'000);
	}
}

static void check_scale(std::string_view encoder_name, video_codec codec, uint16_t width, uint16_t height, std::array<double, 2> & scale)
{
#ifdef WIVRN_USE_NVENC
	if (encoder_name == encoder_nvenc)
	{
		auto max = VideoEncoderNvenc::get_max_size(codec);
		if (width * scale[0] > max[0])
		{
			scale[0] = double(max[0] - 1) / width;
			U_LOG_W("Image is too wide for encoder, reducing scale to %f", scale[0]);
		}
		if (height * scale[1] > max[1])
		{
			scale[1] = double(max[1] - 1) / width;
			U_LOG_W("Image is too tall for encoder, reducing scale to %f", scale[0]);
		}
	}
#endif
}

static std::optional<xrt::drivers::wivrn::video_codec> filter_codecs(wivrn_vk_bundle & bundle, const std::vector<xrt::drivers::wivrn::video_codec> & codecs)
{
	VideoEncoderFFMPEG::mute_logs mute;
	encoder_settings s{
	        {
	                .width = 800,
	                .height = 600,
	                .video_width = 800,
	                .video_height = 600,
	        },
	        "vaapi",
	        default_bitrate,
	};
	for (auto codec: codecs)
	{
		try
		{
			s.codec = codec;
			video_encoder_va test(bundle, s, 60);
			return codec;
		}
		catch (...)
		{}

		U_LOG_I("Video codec %s not supported", std::string(magic_enum::enum_name(codec)).c_str());
	}

	return {};
}

static std::vector<configuration::encoder> get_encoder_default_settings(wivrn_vk_bundle & bundle, const std::vector<xrt::drivers::wivrn::video_codec> & headset_codecs)
{
	if (is_nvidia(*bundle.physical_device))
	{
#ifdef WIVRN_USE_NVENC
		return {{.name = encoder_nvenc}};
#elif defined(WIVRN_USE_X264)
		U_LOG_W("nvidia GPU detected, but nvenc support not compiled");
		return {{.name = encoder_x264, .codec = h264}};
#else
		U_LOG_E("no suitable encoder available (compile with x264 or nvenc support)");
		return {};
#endif
	}
	else
	{
#ifdef WIVRN_USE_VAAPI
		auto codec = filter_codecs(bundle, headset_codecs);
		if (not codec)
		{
			U_LOG_W("Failed to initialize vaapi, fallback to software encoding");
			return {{.name = encoder_x264, .codec = h264}};
		}
#ifdef WIVRN_SPLIT_ENCODERS
		/* Split in 3 parts:
		 *  +--------+--------+
		 *  |        |        |
		 *  |        |        |
		 *  +--------+        |
		 *  |        |        |
		 *  |        |        |
		 *  |        |        |
		 *  |        |        |
		 *  |        |        |
		 *  +--------+--------+
		 * All 3 are encoded sequentially, so that the smallest is ready earlier.
		 * Decoder can start work as fast as possible, reducing idle time.
		 *
		 */
		return {
		        {
		                .name = encoder_vaapi,
		                .width = 0.5,
		                .height = 0.25,
		                .group = 0,
		                .codec = *codec,
		        },
		        {
		                .name = encoder_vaapi,
		                .width = 0.5,
		                .height = 0.75,
		                .offset_y = 0.25,
		                .group = 0,
		                .codec = *codec,
		        },
		        {
		                .name = encoder_vaapi,
		                .width = 0.5,
		                .offset_x = 0.5,
		                .group = 0,
		                .codec = *codec,
		        },
		};
#else
		return {{.name = encoder_vaapi, .codec = *codec}};
#endif
#elif defined(WIVRN_USE_X264)
		U_LOG_W("ffmpeg support not compiled, vaapi encoder not available");
		return {{.name = encoder_x264, .codec = h264}};
#else
		U_LOG_E("no suitable encoder available (compile with x264 or ffmpeg support)");
		return {};
#endif
	}
}

static void make_even(uint16_t & value, uint16_t max)
{
	value += value % 2;
	value = std::min(value, max);
}

std::vector<encoder_settings> xrt::drivers::wivrn::get_encoder_settings(wivrn_vk_bundle & bundle, uint32_t & width, uint32_t & height, const std::vector<xrt::drivers::wivrn::video_codec> & headset_codecs)
{
	configuration config;
	try
	{
		config = configuration::read_user_configuration();
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to read encoder configuration: %s", e.what());
	}
	if (config.encoders.empty())
		config.encoders = get_encoder_default_settings(bundle, headset_codecs);
	uint64_t bitrate = config.bitrate.value_or(default_bitrate);
	auto scale = config.scale.value_or(std::array<double, 2>{0.8, 0.8});
	for (const auto & encoder: config.encoders)
	{
		check_scale(encoder.name,
		            encoder.codec.value_or(xrt::drivers::wivrn::h264),
		            std::ceil(encoder.width.value_or(1) * width),
		            std::ceil(encoder.height.value_or(1) * height),
		            scale);
	}

	width *= scale[0];
	width += width % 2;
	height *= scale[1];
	height += height % 2;

	std::vector<xrt::drivers::wivrn::encoder_settings> res;
	int next_group = 0;
	for (const auto & encoder: config.encoders)
	{
		xrt::drivers::wivrn::encoder_settings settings{};
		settings.encoder_name = encoder.name;
		settings.width = std::ceil(encoder.width.value_or(1) * width);
		settings.height = std::ceil(encoder.height.value_or(1) * height);
		settings.video_width = settings.width;
		settings.video_height = settings.height;
		settings.offset_x = std::ceil(encoder.offset_x.value_or(0) * width);
		settings.offset_y = std::ceil(encoder.offset_y.value_or(0) * height);
		make_even(settings.width, width - settings.offset_x);
		make_even(settings.height, height - settings.offset_y);
		settings.codec = encoder.codec.value_or(xrt::drivers::wivrn::h265);
		settings.group = encoder.group.value_or(next_group);
		settings.options = encoder.options;
		settings.device = encoder.device;

		next_group = std::max(next_group, settings.group + 1);
		res.push_back(settings);
	}
	split_bitrate(res, bitrate);
	return res;
}
