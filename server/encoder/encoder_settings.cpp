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

#include "wivrn_packets.h"
#include <magic_enum.hpp>
#include <string>
#include <vulkan/vulkan.h>

#include "wivrn_config.h"

#if WIVRN_USE_NVENC
#include "video_encoder_nvenc.h"
#endif
#if WIVRN_USE_VAAPI
#include "ffmpeg/video_encoder_va.h"
#include <libavutil/ffversion.h>
#endif

namespace wivrn
{

static const double passthrough_bitrate_factor = 0.05;

static void split_bitrate(std::array<wivrn::encoder_settings, 3> & encoders, uint64_t bitrate)
{
	double total_weight = 0;
	for (auto [i, encoder]: std::ranges::enumerate_view(encoders))
	{
		double w = encoder.width * encoder.height;
		if (i == 2)
			w *= passthrough_bitrate_factor;
		switch (encoder.codec)
		{
			case wivrn::h264:
				w *= 2;
				break;
			case wivrn::h265:
			case wivrn::av1:
			case wivrn::raw:
				break;
		}
		encoder.bitrate = w;
		total_weight += w;
	}

	for (auto & encoder: encoders)
	{
		encoder.bitrate_multiplier = encoder.bitrate / total_weight;
		encoder.bitrate = encoder.bitrate_multiplier * bitrate;
	}
}

void print_encoders(const std::array<wivrn::encoder_settings, 3> & encoders)
{
	int group = -1;
	std::stringstream str;
	str << "Encoder configuration:";
	for (auto & encoder: encoders)
	{
		if (encoder.group != group)
		{
			group = encoder.group;
			str << "\n\t* Group " << group << ":";
		}
		else
			str << "\n";

		str << "\n\t\t" << encoder.encoder_name << " (" << magic_enum::enum_name(encoder.codec) << " " << encoder.bit_depth << "-bit)"
		    << "\n\t\tsize: " << encoder.width << "x" << encoder.height
		    << "\n\t\tbitrate: " << int(encoder.bitrate / 100'000) / 10. << "Mbit/s";
	}
	U_LOG_I("%s", str.str().c_str());
}

static void check_video_size(std::string_view encoder_name, video_codec codec, uint16_t & width, uint16_t & height)
{
#if WIVRN_USE_NVENC
	if (encoder_name == encoder_nvenc)
	{
		auto max = video_encoder_nvenc::get_max_size(codec);
		width = std::min<uint16_t>(max[0], width);
		height = std::min<uint16_t>(max[1], height);
	}
#endif
}

namespace
{
class prober
{
	wivrn_vk_bundle & vk;
	const from_headset::headset_info_packet & info;
	const bool nvidia;

#if WIVRN_USE_VAAPI
	std::unordered_map<video_codec, bool> vaapi_support;

	bool check_vaapi(video_codec codec)
	{
		if (auto it = vaapi_support.find(codec); it != vaapi_support.end())
			return it->second;
		try
		{
			video_encoder_va test(
			        vk,
			        encoder_settings{
			                .width = 800,
			                .height = 800,
			                .codec = codec,
			                .fps = 60,
			                .bitrate = 50'000'000,
			                .bit_depth = 8,
			        },
			        0);
			vaapi_support[codec] = true;
			return true;
		}
		catch (std::exception & e)
		{
			vaapi_support[codec] = false;
			U_LOG_I("vaapi not supported for %s", std::string(magic_enum::enum_name(codec)).c_str());
			return false;
		}
	}
#endif

#if WIVRN_USE_NVENC
	std::unordered_map<video_codec, bool> nvenc_support;

	bool check_nvenc(video_codec codec)
	{
		if (auto it = nvenc_support.find(codec); it != nvenc_support.end())
			return it->second;
		try
		{
			video_encoder_nvenc test(
			        vk,
			        encoder_settings{
			                .width = 800,
			                .height = 800,
			                .codec = codec,
			                .fps = 60,
			                .bitrate = 50'000'000,
			                .bit_depth = 8,
			        },
			        0);
			nvenc_support[codec] = true;
			return true;
		}
		catch (std::exception & e)
		{
			nvenc_support[codec] = false;
			U_LOG_I("nvenc not supported for %s", std::string(magic_enum::enum_name(codec)).c_str());
			return false;
		}
	}
#endif

	static bool is_nvidia(vk::PhysicalDevice physical_device)
	{
		auto props = physical_device.getProperties();
		return props.vendorID == 0x10DE;
	}

#if WIVRN_USE_VULKAN_ENCODE
	bool has_vk_h264()
	{
		if (*vk.encode_queue == VK_NULL_HANDLE)
			return false;
		if (not std::ranges::contains(vk.device_extensions, std::string_view(VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME)))
			return false;

		auto prop = vk.physical_device.getQueueFamilyProperties2<vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyVideoPropertiesKHR>>();
		assert(vk.encode_queue_family_index < prop.size());
		return bool(prop.at(vk.encode_queue_family_index).get<vk::QueueFamilyVideoPropertiesKHR>().videoCodecOperations & vk::VideoCodecOperationFlagBitsKHR::eEncodeH264);
	}
	bool has_vk_h265()
	{
		if (*vk.encode_queue == VK_NULL_HANDLE)
			return false;
		if (not std::ranges::contains(vk.device_extensions, std::string_view(VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME)))
			return false;

		auto prop = vk.physical_device.getQueueFamilyProperties2<vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyVideoPropertiesKHR>>();
		assert(vk.encode_queue_family_index < prop.size());
		return bool(prop.at(vk.encode_queue_family_index).get<vk::QueueFamilyVideoPropertiesKHR>().videoCodecOperations & vk::VideoCodecOperationFlagBitsKHR::eEncodeH265);
	}
#endif

public:
	prober(wivrn_vk_bundle & vk, const from_headset::headset_info_packet & info) :
	        vk(vk), info(info), nvidia(is_nvidia(*vk.physical_device)) {}

	std::pair<std::string, video_codec> select_encoder(const configuration::encoder & config)
	{
		if (config.codec == video_codec::raw or config.name == encoder_raw)
			return {encoder_raw, video_codec::raw};

#if WIVRN_USE_NVENC
		if ((nvidia and config.name.empty()) or config.name == encoder_nvenc)
		{
			for (auto codec: config.codec ? std::vector{*config.codec} : info.supported_codecs)
			{
				if (check_nvenc(codec))
					return {encoder_nvenc, codec};
			}
		}
#endif

#if WIVRN_USE_VULKAN_ENCODE
		if (config.name.empty() or config.name == encoder_vulkan)
		{
			for (auto codec: config.codec ? std::vector{*config.codec} : info.supported_codecs)
			{
				switch (codec)
				{
					case h264:
						if (has_vk_h264())
							return {encoder_vulkan, video_codec::h264};
						U_LOG_I("GPU does not support H.264 Vulkan video encode");
						break;

					case h265:
						if (has_vk_h265())
							return {encoder_vulkan, video_codec::h265};
						U_LOG_I("GPU does not support H.265 Vulkan video encode");
						break;
					case av1:
						U_LOG_D("Vulkan video encode for AV1 is not implemented in WiVRn");
					case raw:
						break;
				}
			}
		}
#endif

#if WIVRN_USE_VAAPI
		if (config.name.empty() or config.name == encoder_vaapi)
		{
			for (auto codec: config.codec ? std::vector{*config.codec} : info.supported_codecs)
			{
				if (check_vaapi(codec))
					return {encoder_vaapi, codec};
			}
		}
#endif
		U_LOG_W("No suitable hardware accelerated codec found");
#if WIVRN_USE_X264
		if (config.name.empty() or config.name == encoder_x264)
			return {encoder_x264, video_codec::h264};
#endif

		throw std::runtime_error("Failed to find a suitable video encoder");
	}
};
} // namespace

static uint16_t align(uint16_t value, uint16_t alignment)
{
	return ((value + alignment - 1) / alignment) * alignment;
}

std::array<encoder_settings, 3> get_encoder_settings(wivrn_vk_bundle & bundle, const from_headset::headset_info_packet & info, const from_headset::settings_changed & settings)
{
	configuration config;

	std::array<wivrn::encoder_settings, 3> res;

	prober prober{bundle, info};
	std::unordered_map<std::string, int> groups;
	int next_group = 0;

	for (auto [src, dst]: std::ranges::zip_view(config.encoders, res))
	{
		dst.fps = settings.preferred_refresh_rate;
		dst.options = src.options;
		dst.device = src.device;

		std::tie(dst.encoder_name, dst.codec) = prober.select_encoder(src);

		auto [it, inserted] = groups.emplace(dst.encoder_name, next_group);
		dst.group = it->second;
		if (inserted)
			++next_group;
	}

	auto width = align(info.stream_eye_width, 64);
	auto height = align(info.stream_eye_height, 64);
	// Ensure we don't try to encode too large images (only for left/right, ignore alpha)
	for (size_t i = 0; i < 2; ++i)
		check_video_size(res[i].encoder_name, res[i].codec, width, height);

	for (auto [i, dst]: std::ranges::enumerate_view(res))
	{
		dst.width = width;
		dst.height = height;
		if (i == 2) // alpha channel
			dst.height /= 2;
	}

	auto bit_depth = config.bit_depth ? config.bit_depth : info.bit_depth;

	if (bit_depth and bit_depth != 8 and bit_depth != 10)
		throw std::runtime_error("invalid bit-depth setting. supported values: 8, 10");

	if (std::ranges::contains(res, video_codec::h264, &encoder_settings::codec) or
	    std::ranges::contains(res, video_codec::raw, &encoder_settings::codec))
		bit_depth = 8;

	for (auto & i: res)
		i.bit_depth = bit_depth.value_or(10);

	split_bitrate(res, settings.bitrate_bps);
	return res;
}
} // namespace wivrn
