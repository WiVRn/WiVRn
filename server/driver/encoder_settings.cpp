// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "encoder_settings.h"
#include "util/u_config_json.h"
#include "video_encoder.h"
#include "vk/vk_helpers.h"

#include <string>
#include <vulkan/vulkan.h>

#ifdef XRT_HAVE_FFMPEG
#include "ffmpeg/VideoEncoderVA.h"
#endif

using namespace xrt::drivers::wivrn;

// TODO: size independent bitrate
static const uint64_t default_bitrate = 10'000'000;

static bool
is_nvidia(vk_bundle *vk)
{
	VkPhysicalDeviceProperties physical_device_properties;
	vk->vkGetPhysicalDeviceProperties(vk->physical_device, &physical_device_properties);
	return physical_device_properties.vendorID == 0x10DE;
}

static std::vector<xrt::drivers::wivrn::encoder_settings>
get_encoder_default_settings(vk_bundle *vk, uint16_t width, uint16_t height)
{
	xrt::drivers::wivrn::encoder_settings settings{};
	settings.width = width;
	settings.height = height;
	settings.codec = xrt::drivers::wivrn::h265;
	settings.bitrate = default_bitrate;

	if (is_nvidia(vk)) {
#ifdef XRT_HAVE_CUDA
		settings.encoder_name = encoder_nvenc;
#elif defined(XRT_HAVE_X264)
		settings.encoder_name = encoder_x264;
		settings.codec = xrt::drivers::wivrn::h264;
		U_LOG_W("nvidia GPU detected, but cuda support not compiled");
#else
		U_LOG_E("no suitable encoder available (compile with x264 or cuda support)");
		return {};
#endif
	} else {
#ifdef XRT_HAVE_FFMPEG
		settings.encoder_name = encoder_vaapi;
#elif defined(XRT_HAVE_X264)
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

std::vector<encoder_settings>
xrt::drivers::wivrn::get_encoder_settings(vk_bundle *vk, uint16_t width, uint16_t height)
{
	u_config_json config_json{};
	std::unique_ptr<u_config_json, void (*)(u_config_json *)> raii(&config_json, u_config_json_close);

	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		U_LOG_D("no config file, using default");
		return get_encoder_default_settings(vk, width, height);
	}

	const cJSON *config_wivrn = u_json_get(config_json.root, "config_wivrn");
	if (config_wivrn == NULL) {
		U_LOG_D("no config_wivrn entry, using default");
		return get_encoder_default_settings(vk, width, height);
	}

	const cJSON *config_encoder = u_json_get(config_wivrn, "encoders");
	if (config_encoder == NULL) {
		U_LOG_D("no config_wivrn.encoders entry, using default");
		return get_encoder_default_settings(vk, width, height);
	}
	std::vector<xrt::drivers::wivrn::encoder_settings> res;
	int num_encoders = cJSON_GetArraySize(config_encoder);

	int next_group = 0;

	for (int i = 0; i < num_encoders; ++i) {
		cJSON *encoder = cJSON_GetArrayItem(config_encoder, i);
		xrt::drivers::wivrn::encoder_settings settings{};

		auto name = u_json_get(encoder, "encoder");
		if (name && cJSON_IsString(name))
			settings.encoder_name = name->valuestring;
		else {
			U_LOG_E("missing \"encoder\" key in config_wivrn");
			continue;
		}

		settings.width = width;
		settings.height = height;
		settings.bitrate = default_bitrate;
		settings.codec = xrt::drivers::wivrn::h264;
		settings.group = next_group;

		auto js_width = u_json_get(encoder, "width");
		if (js_width && cJSON_IsNumber(js_width))
			settings.width = std::ceil(js_width->valuedouble * width);

		auto js_height = u_json_get(encoder, "height");
		if (js_height && cJSON_IsNumber(js_height))
			settings.height = std::ceil(js_height->valuedouble * height);

		auto offset_x = u_json_get(encoder, "offset_x");
		if (offset_x && cJSON_IsNumber(offset_x))
			settings.offset_x = std::floor(offset_x->valuedouble * width);
		auto offset_y = u_json_get(encoder, "offset_y");
		if (offset_y && cJSON_IsNumber(offset_y))
			settings.offset_y = std::floor(offset_y->valuedouble * height);

		auto bitrate = u_json_get(encoder, "bitrate");
		if (bitrate && cJSON_IsNumber(bitrate))
			settings.bitrate = bitrate->valueint;

		auto group = u_json_get(encoder, "group");
		if (group && cJSON_IsNumber(group))
			settings.group = group->valueint;

		auto codec = u_json_get(encoder, "codec");
		if (codec && cJSON_IsString(codec)) {
			if (codec->valuestring == std::string("h264"))
				settings.codec = xrt::drivers::wivrn::h264;
			else if (codec->valuestring == std::string("h265"))
				settings.codec = xrt::drivers::wivrn::h265;
			else {
				U_LOG_E("invalid video codec %s", codec->valuestring);
				continue;
			}
		}
		auto options = u_json_get(encoder, "options");
		if (options && cJSON_IsObject(options)) {
			for (auto child = options->child; child; child = child->next) {
				if (cJSON_IsString(child)) {
					settings.options[child->string] = child->valuestring;
				}
			}
		}

		next_group = std::max(next_group, settings.group + 1);

		res.push_back(settings);
	}

	return res;
}

VkImageTiling
xrt::drivers::wivrn::get_required_tiling(vk_bundle *vk,
                                         const std::vector<xrt::drivers::wivrn::encoder_settings> &settings)
{

	bool can_optimal = true;
	bool can_drm = vk->has_EXT_image_drm_format_modifier;
	for (const auto &item : settings) {
#ifdef XRT_HAVE_FFMPEG
		if (item.encoder_name == encoder_vaapi) {
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
