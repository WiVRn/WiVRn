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
#include <charconv>
#include <cmath>
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
// TODO: size independent bitrate
static const uint64_t default_bitrate = 50'000'000;

static const double passthrough_bitrate_factor = 0.05;

static bool is_nvidia(vk::PhysicalDevice physical_device)
{
	auto props = physical_device.getProperties();
	return props.vendorID == 0x10DE;
}

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
		    << "\n\t\tbitrate: " << encoder.bitrate / 1'000'000 << "Mbit/s";
	}
	U_LOG_I("%s", str.str().c_str());
}

static void check_scale(std::string_view encoder_name, video_codec codec, uint16_t width, uint16_t height, std::array<double, 2> & scale)
{
#if WIVRN_USE_NVENC
	if (encoder_name == encoder_nvenc)
	{
		auto max = video_encoder_nvenc::get_max_size(codec);
		if (width * scale[0] > max[0])
		{
			scale[0] = double(max[0] - 1) / width;
			U_LOG_W("Image is too wide for encoder, reducing scale to %f", scale[0]);
		}
		if (height * scale[1] > max[1])
		{
			scale[1] = double(max[1] - 1) / width;
			U_LOG_W("Image is too tall for encoder, reducing scale to %f", scale[1]);
		}
	}
#endif
}

#if WIVRN_USE_VAAPI

static constexpr auto ffmpeg_version()
{
	std::array<int, 3> result;
	std::string_view version = FFMPEG_VERSION;
	for (auto & item: result)
	{
		auto dot = version.find(".");
		auto number = version.substr(version.find_first_of("0123456789"), dot);
		version = version.substr(dot + 1);
		auto res = std::from_chars(number.begin(), number.end(), item);
		if (res.ec != std::errc{})
			throw std::invalid_argument("Failed to parse FFMPEG_VERSION " FFMPEG_VERSION);
	}
	return result;
}

static std::optional<wivrn::video_codec> filter_codecs_vaapi(wivrn_vk_bundle & bundle, const std::vector<wivrn::video_codec> & codecs, int bit_depth)
{
	video_encoder_ffmpeg::mute_logs mute;
	encoder_settings s{
	        .width = 800,
	        .height = 600,
	        .fps = 60,
	        .encoder_name = "vaapi",
	        .bitrate = default_bitrate,
	        .bit_depth = bit_depth,
	};

	for (auto codec: codecs)
	{
		if (codec == wivrn::video_codec::h264)
		{
			if (bit_depth != 8)
			{
				U_LOG_D("Will not use h264: %d-bit not supported", bit_depth);
				continue;
			}
			if constexpr (ffmpeg_version()[0] < 6)
			{
				U_LOG_W("Skip h264 on ffmpeg < 6 due to poor performance");
				continue;
			}
		}
		try
		{
			s.codec = codec;
			video_encoder_va test(bundle, s, 0);
			return codec;
		}
		catch (...)
		{}

		U_LOG_I("Video codec %s not supported", std::string(magic_enum::enum_name(codec)).c_str());
	}

	return {};
}
#endif

#if WIVRN_USE_NVENC
static bool probe_nvenc(wivrn_vk_bundle & bundle, int bit_depth)
{
	static bool res = [&]() {
	encoder_settings s{
		.width = 800,
			.height = 608,
			.codec = h264,
			.fps = 60,
			.encoder_name = encoder_nvenc,
			.bitrate = default_bitrate,
			.bit_depth = bit_depth,
	};
	try
	{
		video_encoder_nvenc test(bundle, s, 0);
		return true;
	}
	catch (std::exception & e)
	{
		U_LOG_W("nvenc not supported: %s", e.what());
		return false;
	} }();
	return res;
}
#endif

static void fill_defaults(wivrn_vk_bundle & bundle, const std::vector<wivrn::video_codec> & headset_codecs, configuration::encoder & config, int bit_depth)
{
	if (config.name.empty())
	{
		if (is_nvidia(*bundle.physical_device))
		{
#if WIVRN_USE_NVENC
			if (probe_nvenc(bundle, bit_depth))
				config.name = encoder_nvenc;
			else
#else
			U_LOG_W("nvidia GPU detected, but nvenc support not compiled");
#endif
			{
#if WIVRN_USE_X264
				if (bit_depth != 8)
					U_LOG_E("no encoder found with %d-bit support (set 8-bit to use x264)", bit_depth);
				else
				{
					config.name = encoder_x264;
					config.codec = h264;
				}
#else
				U_LOG_E("no suitable encoder available (compile with x264 or nvenc support)");
#endif
			}
		}
		else
		{
#if WIVRN_USE_VAAPI
			config.name = encoder_vaapi;
#elif WIVRN_USE_X264
			if (bit_depth != 8)
				U_LOG_E("no encoder found with %d-bit support (set 8-bit to use x264)", bit_depth);
			else
			{
				U_LOG_W("ffmpeg support not compiled, vaapi encoder not available");
				config.name = encoder_x264;
				config.codec = h264;
			}
#else
			U_LOG_E("no suitable encoder available (compile with x264 or nvenc support)");
#endif
		}
	}

#if WIVRN_USE_VAAPI
	if (config.name == encoder_vaapi and not config.codec)
	{
		config.codec = filter_codecs_vaapi(bundle, headset_codecs, bit_depth);
		if (not config.codec)
		{
#if WIVRN_USE_X264
			if (bit_depth != 8)
				U_LOG_E("Failed to initialize vaapi, but can't use x264 due to %d-bit encoding (set 8-bit to use x264)", bit_depth);
			else
			{
				U_LOG_W("Failed to initialize vaapi, fallback to software encoding");
				config.name = encoder_x264;
				config.codec = h264;
			}
#else
			U_LOG_E("Failed to initialize vaapi");
#endif
		}
	}
#endif

	if (config.name == encoder_vulkan and not config.codec)
	{
		if (bit_depth == 10)
			config.codec = h265;
		else
			config.codec = h264;
	}

#if WIVRN_USE_X264
	if (config.name == encoder_x264)
		config.codec = h264; // this will fail if 10-bit is enabled
#endif

	if (config.name == encoder_raw)
		config.codec = raw;

	if (not config.codec)
		config.codec = bit_depth == 10 ? h265 : h264;
}

static std::array<configuration::encoder, 3> get_encoder_default_settings(wivrn_vk_bundle & bundle, const std::vector<wivrn::video_codec> & headset_codecs, int bit_depth)
{
	configuration::encoder base;
	fill_defaults(bundle, headset_codecs, base, bit_depth);
	return {base, base, base};
}

static uint16_t align(uint16_t value, uint16_t alignment)
{
	return ((value + alignment - 1) / alignment) * alignment;
}

std::array<encoder_settings, 3> get_encoder_settings(wivrn_vk_bundle & bundle, uint32_t & width, uint32_t & height, const from_headset::headset_info_packet & info)
{
	configuration config;

	if (config.bit_depth != 8 && config.bit_depth != 10)
		throw std::runtime_error("invalid bit-depth setting. supported values: 8, 10");

	if (config.encoders.empty())
		config.encoders = get_encoder_default_settings(bundle, info.supported_codecs, config.bit_depth);

	uint64_t bitrate = config.bitrate.value_or(default_bitrate);
	std::array<double, 2> default_scale;
	default_scale.fill(info.eye_gaze ? 0.35 : 0.5);
	auto scale = config.scale.value_or(default_scale);

	for (auto & encoder: config.encoders)
	{
		fill_defaults(bundle, info.supported_codecs, encoder, config.bit_depth);
		assert(encoder.codec);
		check_scale(encoder.name,
		            *encoder.codec,
		            std::ceil(width),
		            std::ceil(height),
		            scale);
	}

	width = align(width * scale[0], 64);
	height = align(height * scale[1], 64);

	std::array<wivrn::encoder_settings, 3> res;
	std::unordered_map<std::string, int> groups;
	int next_group = 0;
	for (const auto & encoder: config.encoders)
	{
		if (encoder.group)
		{
			groups[encoder.name] = *encoder.group;
			next_group = std::max(next_group, *encoder.group + 1);
		}
	}

	for (const auto & [i, encoder]: std::ranges::enumerate_view(config.encoders))
	{
		wivrn::encoder_settings settings{};
		settings.encoder_name = encoder.name;
		settings.width = align(std::ceil(width), 32);
		settings.height = align(std::ceil(height), 32);
		// alpha is half resolution, but left and right side by side
		if (i == 2)
			settings.height /= 2;
		settings.codec = *encoder.codec;
		settings.bit_depth = config.bit_depth;
		if (encoder.group)
			settings.group = *encoder.group;
		else
		{
			auto [it, inserted] = groups.emplace(encoder.name, next_group);
			settings.group = it->second;
			if (inserted)
				++next_group;
		}
		settings.options = encoder.options;
		settings.device = encoder.device;
		settings.fps = info.preferred_refresh_rate;

		res[i] = settings;
	}
	split_bitrate(res, bitrate);
	return res;
}
} // namespace wivrn
