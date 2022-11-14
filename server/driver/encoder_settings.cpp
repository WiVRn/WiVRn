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
#include "video_encoder.h"
#include "vk/vk_helpers.h"
#include "configuration.h"

#include <string>
#include <vulkan/vulkan.h>

#include "wivrn_config.h"

#ifdef WIVRN_HAVE_FFMPEG
#include "ffmpeg/VideoEncoderVA.h"
#endif

using namespace xrt::drivers::wivrn;

// TODO: size independent bitrate
static const uint64_t default_bitrate = 10'000'000;

static bool is_nvidia(vk_bundle * vk)
{
	VkPhysicalDeviceProperties physical_device_properties;
	vk->vkGetPhysicalDeviceProperties(vk->physical_device, &physical_device_properties);
	return physical_device_properties.vendorID == 0x10DE;
}

static std::vector<xrt::drivers::wivrn::encoder_settings> get_encoder_default_settings(vk_bundle * vk, uint16_t width, uint16_t height)
{
	xrt::drivers::wivrn::encoder_settings settings{};
	settings.width = width;
	settings.height = height;
	settings.codec = xrt::drivers::wivrn::h265;
	settings.bitrate = default_bitrate;

	if (is_nvidia(vk))
	{
#ifdef WIVRN_HAVE_CUDA
		settings.encoder_name = encoder_nvenc;
#elif defined(WIVRN_HAVE_X264)
		settings.encoder_name = encoder_x264;
		settings.codec = xrt::drivers::wivrn::h264;
		U_LOG_W("nvidia GPU detected, but cuda support not compiled");
#else
		U_LOG_E("no suitable encoder available (compile with x264 or cuda support)");
		return {};
#endif
	}
	else
	{
#ifdef WIVRN_HAVE_FFMPEG
		settings.encoder_name = encoder_vaapi;
#elif defined(WIVRN_HAVE_X264)
		settings.encoder_name = encoder_x264;
		settings.codec = xrt::drivers::wivrn::h264;
		U_LOG_W("ffmpeg support not compiled, vaapi encoder not available");
#else
		U_LOG_E("no suitable encoder available (compile with x264 or ffmpeg support)");
		return {};
#endif
	}

	return {settings};
}

std::vector<encoder_settings> xrt::drivers::wivrn::get_encoder_settings(vk_bundle * vk, uint16_t width, uint16_t height)
{
	try
	{
		const auto & config = configuration::read_user_configuration();
		if (config.encoders.empty())
			return get_encoder_default_settings(vk, width, height);
		std::vector<xrt::drivers::wivrn::encoder_settings> res;
		int next_group = 0;
		for (const auto& encoder: config.encoders)
		{
			xrt::drivers::wivrn::encoder_settings settings{};
			settings.encoder_name = encoder.name;
			settings.width = std::ceil(encoder.width.value_or(1) * width);
			settings.height = std::ceil(encoder.height.value_or(1) * height);
			settings.offset_x = std::ceil(encoder.offset_x.value_or(0) * width);
			settings.offset_y = std::ceil(encoder.offset_y.value_or(0) * height);
			settings.bitrate = encoder.bitrate.value_or(default_bitrate);
			settings.codec = encoder.codec.value_or(xrt::drivers::wivrn::h264);
			settings.group = encoder.group.value_or(next_group);
			settings.options = encoder.options;

			next_group = std::max(next_group, settings.group + 1);
			res.push_back(settings);
		}
		return res;
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to read encoder configuration: %s", e.what());
	}
	return get_encoder_default_settings(vk, width, height);
}

VkImageTiling xrt::drivers::wivrn::get_required_tiling(vk_bundle * vk,
                                                       const std::vector<xrt::drivers::wivrn::encoder_settings> & settings)
{
	bool can_optimal = true;
	bool can_drm = vk->has_EXT_image_drm_format_modifier;
	for (const auto & item: settings)
	{
#ifdef WIVRN_HAVE_FFMPEG
		if (item.encoder_name == encoder_vaapi)
		{
			can_optimal = false;
			if (not use_drm_format_modifiers)
				can_drm = false;
		}
#endif
	}
	if (can_optimal)
		return VK_IMAGE_TILING_OPTIMAL;
	if (can_drm)
		return VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
	return VK_IMAGE_TILING_LINEAR;
}
