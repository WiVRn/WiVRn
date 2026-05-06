/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "hardware.h"
#include "xr/to_string.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <limits>
#include <string>

#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include "utils/wrap_lambda.h"

#include <sys/system_properties.h>

static std::optional<std::string> get_property(const char * property)
{
	auto info = __system_property_find(property);
	if (not info)
		return std::nullopt;

	std::string result;
	wrap_lambda cb = [&result](const char * name, const char * value, uint32_t serial) {
		result = value;
	};

	__system_property_read_callback(info, cb.userdata_first(), cb);
	return result;
}
#endif

static std::optional<std::string> env_string(const char * env_name, const char * android_sysprop_name)
{
#ifdef __ANDROID__
	return get_property(android_sysprop_name);
#else
	const char * value = std::getenv(env_name);
	if (value)
		return value;
	return std::nullopt;
#endif
}

static std::optional<uint32_t> env_u32(const char * env_name, const char * android_sysprop_name)
{
	const auto value = env_string(env_name, android_sysprop_name);
	if (not value)
		return std::nullopt;
	try
	{
		const unsigned long parsed = std::stoul(*value);
		if (parsed > std::numeric_limits<uint32_t>::max())
			return std::nullopt;
		return static_cast<uint32_t>(parsed);
	}
	catch (std::runtime_error & e)
	{
		spdlog::warn("failed to parse {} (value: {}: {}",
#ifdef __ANDROID__
		             android_sysprop_name,
#else
		             env_name,
#endif
		             *value,
		             e.what()

		);
		return std::nullopt;
	}
}

static std::optional<bool> env_bool(const char * env_name, const char * android_sysprop_name)
{
	const auto value = env_string(env_name, android_sysprop_name);
	if (not value)
		return std::nullopt;
	if (strcmp(value->c_str(), "1") == 0 || strcasecmp(value->c_str(), "true") == 0 || strcasecmp(value->c_str(), "yes") == 0 || strcasecmp(value->c_str(), "on") == 0)
		return true;
	if (strcmp(value->c_str(), "0") == 0 || strcasecmp(value->c_str(), "false") == 0 || strcasecmp(value->c_str(), "no") == 0 || strcasecmp(value->c_str(), "off") == 0)
		return false;
	return std::nullopt;
}

const hmd_traits_t hmd_traits = [] {
	hmd_traits_t traits{};

#ifdef __ANDROID__
	const auto device = get_property("ro.product.device");
	const auto manufacturer = get_property("ro.product.manufacturer");
	const auto model = get_property("ro.product.model");

	spdlog::info("Guessing HMD model from:");
	spdlog::info("    ro.product.device = \"{}\":", device.value_or("<unset>"));
	spdlog::info("    ro.product.manufacturer = \"{}\":", manufacturer.value_or("<unset>"));
	spdlog::info("    ro.product.model = \"{}\":", model.value_or("<unset>"));

	traits.permissions[feature::microphone] = "android.permission.RECORD_AUDIO";

	if (manufacturer == "Oculus")
	{
		traits.permissions[feature::eye_gaze] = "com.oculus.permission.EYE_TRACKING";
		traits.permissions[feature::face_tracking] = "com.oculus.permission.FACE_TRACKING";

		// Quest hand tracking creates a fake khr/simple_controller when hand tracking
		// is enabled, this messes with native hand tracking
		traits.bind_simple_controller = false;

		// Quest breaks spec and does not support grip_surface for ext/hand_interaction_ext
		traits.hand_interaction_grip_surface = false;

		if (device == "monterey") // Quest 1
		{
			traits.panel_width_override = 1440;
			traits.controller_profile = "oculus-touch-v2";
			traits.vk_debug_ext_allowed = false; // Quest 1 lies, the extension won't load
		}
		else if (device == "hollywood") // Quest 2
		{
			traits.panel_width_override = 1832;
			traits.controller_profile = "oculus-touch-v3";
		}
		else if (device == "seacliff")
		{
			traits.panel_width_override = 1800; // Quest pro
			traits.controller_profile = "meta-quest-touch-pro";
		}
		else if (device == "eureka")
		{
			traits.panel_width_override = 2064; // Quest 3
			traits.controller_profile = "meta-quest-touch-plus";
		}
		else if (device == "panther") // Quest 3S
		{
			traits.panel_width_override = 1832;
			traits.controller_profile = "meta-quest-touch-plus";
		}
	}

	else if (model == "Lynx-R1")
	{
		traits.needs_srgb_conversion = false;
	}

	else if (manufacturer == "Pico")
	{
		traits.permissions[feature::eye_gaze] = "com.picovr.permission.EYE_TRACKING";
		traits.permissions[feature::face_tracking] = "com.picovr.permission.FACE_TRACKING";
		traits.view_locate = false;
		traits.discard_frame = false;
		const auto pico_model = get_property("pxr.vendorhw.product.model");
		spdlog::info("    pxr.vendorhw.product.model = \"{}\":", pico_model.value_or("<unset>"));

		if (pico_model == "Pico Neo 3")
		{
			traits.panel_width_override = 1832;
			traits.controller_profile = "pico-neo3";
		}

		else if (pico_model and pico_model->starts_with("PICO 4"))
		{
			traits.panel_width_override = 2160;
			if (pico_model == "PICO 4 Ultra")
				traits.controller_profile = "pico-4u";
			else
				traits.controller_profile = "pico-4";

			if (pico_model == "PICO 4 Pro" or pico_model == "PICO 4 Enterprise")
				traits.pico_face_tracker = true;
		}
	}
	if (manufacturer == "HTC")
	{
		// Accepts OpenXR 1.1 but doesn't actually implement it
		traits.max_openxr_api_version = XR_API_VERSION_1_0;
		// Doesn't handle additive blend, so needs specific ray model
		traits.controller_ray_model = "assets://ray-htc.glb";

		traits.controller_profile = "htc-vive-focus-3";

		if (model == "VIVE Focus 3")
			traits.panel_width_override = 2448;

		if (model == "VIVE Focus Vision")
			traits.panel_width_override = 2448;

		if (model == "VIVE XR Series")
			traits.panel_width_override = 1920;
	}
	if (manufacturer == "samsung")
	{
		if (model == "SM-I610") // Galaxy XR
		{
			traits.panel_width_override = 3552;
			traits.controller_profile = "samsung-galaxyxr";
			traits.permissions[feature::hand_tracking] = "android.permission.HAND_TRACKING";
			traits.permissions[feature::eye_gaze] = "android.permission.EYE_TRACKING_FINE";
			traits.permissions[feature::face_tracking] = "android.permission.FACE_TRACKING";
		}
	}
#endif

	if (auto panel_width = env_u32("WIVRN_QUIRK_PANEL_WIDTH", "debug.wivrn.quirk_panel_width"))
	{
		spdlog::info("panel width override: {} -> {}", traits.panel_width_override, *panel_width);
		traits.panel_width_override = *panel_width;
	}

	if (auto disable_openxr_1_1 = env_bool("WIVRN_QUIRK_DISABLE_OPENXR_1_1", "debug.wivrn.quirk_disable_openxr_1_1"))
	{
		auto old = traits.max_openxr_api_version;
		traits.max_openxr_api_version = *disable_openxr_1_1 ? XR_API_VERSION_1_0 : XR_API_VERSION_1_1;
		spdlog::info("max OpenXR version override: {} -> {}", xr::to_string(old), xr::to_string(traits.max_openxr_api_version));
	}

	if (auto srgb = env_bool("WIVRN_QUIRK_SRGB_CONVERSION", "debug.wivrn.quirk_srgb_conversion"))
	{
		spdlog::info("srgb conversion override: {} -> {}", traits.needs_srgb_conversion, *srgb);
		traits.needs_srgb_conversion = *srgb;
	}

	if (auto controller = env_string("WIVRN_CONTROLLER", "debug.wivrn.controller"))
	{
		spdlog::info("controller override: {} -> {}", traits.controller_profile, *controller);
		traits.controller_profile = *controller;
	}

	spdlog::info("Initialized HMD traits: profile={}, ray_model={}, panel_width_override={}, max_openxr_api={}, needs_srgb_conversion={}",
	             traits.controller_profile,
	             traits.controller_ray_model,
	             traits.panel_width_override,
	             xr::to_string(traits.max_openxr_api_version),
	             traits.needs_srgb_conversion);

	return traits;
}();

std::string model_name()
{
#ifdef __ANDROID__
	const auto manufacturer = get_property("ro.product.manufacturer").value_or("");
	const auto model = get_property("ro.product.model").value_or("");

	return manufacturer + " " + model;
#else
	return "Unknown headset";
#endif
}

static XrViewConfigurationView scale_view(XrViewConfigurationView view, uint32_t width)
{
	double ratio = double(width) / view.recommendedImageRectWidth;
	view.recommendedImageRectWidth = width;
	view.recommendedImageRectHeight *= ratio;
	spdlog::info("Using panel size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	return view;
}

XrViewConfigurationView hmd_traits_t::override_view(XrViewConfigurationView view) const
{
	// Standalone headsets tend to report a lower resolution
	// as the GPU can't handle full res.
	// Return the panel resolution instead.
	spdlog::debug("Recommended image size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	if (panel_width_override > 0)
		return scale_view(view, panel_width_override);
	return view;
}

std::pair<glm::vec3, glm::quat> controller_offset(std::string_view profile, xr::spaces space)
{
#ifndef __ANDROID__
	if (const char * offset = [space]() -> const char * {
		    switch (space)
		    {
			    case xr::spaces::grip_left:
				    return std::getenv("WIVRN_GRIP_LEFT_OFFSET");
			    case xr::spaces::grip_right:
				    return std::getenv("WIVRN_GRIP_RIGHT_OFFSET");
			    case xr::spaces::aim_left:
				    return std::getenv("WIVRN_AIM_LEFT_OFFSET");
			    case xr::spaces::aim_right:
				    return std::getenv("WIVRN_AIM_RIGHT_OFFSET");
			    default:
				    return nullptr;
		    }
	    }())
	{
		float x, y, z, yaw, pitch, roll;
		if (sscanf(offset, "%f %f %f %f %f %f", &x, &y, &z, &yaw, &pitch, &roll) == 6)
		{
			return {{x, y, z},
			        glm::angleAxis(glm::radians(yaw), glm::vec3{0, 1, 0}) *
			                glm::angleAxis(glm::radians(pitch), glm::vec3{1, 0, 0}) *
			                glm::angleAxis(glm::radians(roll), glm::vec3{0, 0, 1})};
		}
	}
#endif

	if (profile == "oculus-touch-v2")
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.006, -0.025}, glm::angleAxis(glm::radians(-15.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{-0.010, 0, 0.02}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.010, 0, 0.02}, {1, 0, 0, 0}};

			default:
				break;
		}
	else if (profile == "meta-quest-touch-plus")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.010, -0.025}, glm::angleAxis(glm::radians(-12.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
			case xr::spaces::aim_right:
				return {{0, 0, -0.03}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "htc-vive-focus-3")
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, 0.007, -0.030}, {1, 0, 0, 0}};

			case xr::spaces::aim_left:
			case xr::spaces::aim_right:
				return {{0, -0.025, 0.005}, {1, 0, 0, 0}};

			default:
				break;
		}
	else if (profile == "pico-4" || profile == "pico-4s")
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.014, -0.048}, glm::angleAxis(glm::radians(-35.060f), glm::vec3{1, 0, 0})};

			default:
				break;
		}

	else if (profile == "pico-neo3")
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, 0.005, -0.051}, glm::angleAxis(glm::radians(-29.400f), glm::vec3{1, 0, 0})};

			default:
				break;
		}

	else if (profile == "samsung-galaxyxr")
		switch (space)
		{
			case xr::spaces::grip_left:
				return {{-0.005, 0.000, 0.035}, {1, 0, 0, 0}};
			case xr::spaces::grip_right:
				return {{0.005, 0.000, 0.035}, {1, 0, 0, 0}};
			default:
				break;
		}

	return {{0, 0, 0}, {1, 0, 0, 0}};
}
