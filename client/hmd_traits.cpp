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

#include "hmd_traits.h"

#include "xr/to_string.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <limits>
#include <optional>
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

static std::optional<std::string> env_string(std::string_view name)
{
#ifdef __ANDROID__
	auto android_sysprop_name = std::format("wivrn.debug.{}", name);
	return get_property(android_sysprop_name.c_str());
#else
	std::string env_name = "WIVRN_";
	env_name.reserve(env_name.size() + name.size());
	for (const auto & c: name)
		env_name += std::toupper(c);
	const char * value = std::getenv(env_name.c_str());
	if (value)
		return value;
	return std::nullopt;
#endif
}

static std::optional<uint32_t> env_u32(std::string_view name)
{
	const auto value = env_string(name);
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
		spdlog::warn("failed to parse {} (value: {}: {}", name, *value, e.what());
		return std::nullopt;
	}
}

static std::optional<bool> env_bool(std::string_view name)
{
	const auto value = env_string(name);
	if (not value)
		return std::nullopt;
	if (strcmp(value->c_str(), "1") == 0 || strcasecmp(value->c_str(), "true") == 0 || strcasecmp(value->c_str(), "yes") == 0 || strcasecmp(value->c_str(), "on") == 0)
		return true;
	if (strcmp(value->c_str(), "0") == 0 || strcasecmp(value->c_str(), "false") == 0 || strcasecmp(value->c_str(), "no") == 0 || strcasecmp(value->c_str(), "off") == 0)
		return false;
	return std::nullopt;
}

hmd_traits::hmd_traits() = default;

void hmd_traits::init()
{
#ifndef NDEBUG
	assert(not initialized_);
	initialized_ = true;
#endif
#ifdef __ANDROID__
	const auto device = get_property("ro.product.device");
	const auto manufacturer = get_property("ro.product.manufacturer");
	const auto model = get_property("ro.product.model");

	spdlog::info("Guessing HMD model from:");
	spdlog::info("    ro.product.device = \"{}\":", device.value_or("<unset>"));
	spdlog::info("    ro.product.manufacturer = \"{}\":", manufacturer.value_or("<unset>"));
	spdlog::info("    ro.product.model = \"{}\":", model.value_or("<unset>"));

	permissions[feature::microphone] = "android.permission.RECORD_AUDIO";

	if (manufacturer == "Oculus")
	{
		permissions[feature::eye_gaze] = "com.oculus.permission.EYE_TRACKING";
		permissions[feature::face_tracking] = "com.oculus.permission.FACE_TRACKING";

		// Quest hand tracking creates a fake khr/simple_controller when hand tracking
		// is enabled, this messes with native hand tracking
		bind_simple_controller = false;

		// Quest breaks spec and does not support grip_surface for ext/hand_interaction_ext
		hand_interaction_grip_surface = false;

		if (device == "monterey") // Quest 1
		{
			panel_width_override = 1440;
			controller_profile = "oculus-touch-v2";
			vk_debug_ext_allowed = false; // Quest 1 lies, the extension won't load
		}
		else if (device == "hollywood") // Quest 2
		{
			panel_width_override = 1832;
			controller_profile = "oculus-touch-v3";
		}
		else if (device == "seacliff")
		{
			panel_width_override = 1800; // Quest pro
			controller_profile = "meta-quest-touch-pro";
		}
		else if (device == "eureka")
		{
			panel_width_override = 2064; // Quest 3
			controller_profile = "meta-quest-touch-plus";
		}
		else if (device == "panther") // Quest 3S
		{
			panel_width_override = 1832;
			controller_profile = "meta-quest-touch-plus";
		}
	}

	else if (model == "Lynx-R1")
	{
		needs_srgb_conversion = false;
	}

	else if (manufacturer == "Pico")
	{
		permissions[feature::eye_gaze] = "com.picovr.permission.EYE_TRACKING";
		permissions[feature::face_tracking] = "com.picovr.permission.FACE_TRACKING";
		view_locate = false;
		discard_frame = false;
		const auto pico_model = get_property("pxr.vendorhw.product.model");
		spdlog::info("    pxr.vendorhw.product.model = \"{}\":", pico_model.value_or("<unset>"));

		if (pico_model == "Pico Neo 3")
		{
			panel_width_override = 1832;
			controller_profile = "pico-neo3";
		}

		else if (pico_model and pico_model->starts_with("PICO 4"))
		{
			panel_width_override = 2160;
			if (pico_model == "PICO 4 Ultra")
				controller_profile = "pico-4u";
			else
				controller_profile = "pico-4";

			if (pico_model == "PICO 4 Pro" or pico_model == "PICO 4 Enterprise")
				pico_face_tracker = true;
		}
	}
	if (manufacturer == "HTC")
	{
		// Accepts OpenXR 1.1 but doesn't actually implement it
		max_openxr_api_version = XR_API_VERSION_1_0;
		// Doesn't handle additive blend, so needs specific ray model
		controller_ray_model = "assets://ray-htc.glb";

		controller_profile = "htc-vive-focus-3";

		if (model == "VIVE Focus 3")
			panel_width_override = 2448;

		if (model == "VIVE Focus Vision")
			panel_width_override = 2448;

		if (model == "VIVE XR Series")
			panel_width_override = 1920;
	}
	if (manufacturer == "samsung")
	{
		if (model == "SM-I610") // Galaxy XR
		{
			panel_width_override = 3552;
			controller_profile = "samsung-galaxyxr";
			permissions[feature::hand_tracking] = "android.permission.HAND_TRACKING";
			permissions[feature::eye_gaze] = "android.permission.EYE_TRACKING_FINE";
			permissions[feature::face_tracking] = "android.permission.FACE_TRACKING";
		}
	}
#endif

	spdlog::info("HMD traits initialized");
	auto log_string = [](std::string_view name, std::string & field) {
		if (auto val = env_string(name))
		{
			spdlog::info("\t{} override: {} -> {}", name, field, *val);
			field = *val;
		}
		else
		{
			spdlog::info("\t{}: {}", name, field);
		}
	};
	auto log_u32 = [](std::string_view name, uint32_t & field) {
		if (auto val = env_u32(name))
		{
			spdlog::info("\t{} override: {} -> {}", name, field, *val);
			field = *val;
		}
		else
		{
			spdlog::info("\t{}: {}", name, field);
		}
	};
	auto log_bool = [](std::string_view name, bool & field) {
		if (auto val = env_bool(name))
		{
			spdlog::info("\t{} override: {} -> {}", name, field, *val);
			field = *val;
		}
		else
		{
			spdlog::info("\t{}: {}", name, field);
		}
	};

	log_string("controller_profile", controller_profile);
	log_string("controller_ray_model", controller_ray_model);
	if (auto val = env_string("max_openxr_api_version"))
	{
		XrVersion v;
		if (val == "1.1")
			v = XR_API_VERSION_1_1;
		else if (val == "1.0")
			v = XR_API_VERSION_1_0;
		else
		{
			spdlog::warn("XrVersion {} not recognized", *val);
			v = max_openxr_api_version;
		}
		spdlog::info("\t{} override: {} -> {}", "max_openxr_api_version", xr::to_string(max_openxr_api_version), xr::to_string(v));
		max_openxr_api_version = v;
	}
	else
	{
		spdlog::info("\t{}: {}", "max_openxr_api_version", xr::to_string(max_openxr_api_version));
	}
	log_u32("panel_width_override", panel_width_override);
	log_bool("needs_srgb_conversion", needs_srgb_conversion);
	log_bool("view_locate", view_locate);
	log_bool("vk_debug_ext_allowed", vk_debug_ext_allowed);
	log_bool("bind_simple_controller", bind_simple_controller);
	log_bool("hand_interaction_grip_surface", hand_interaction_grip_surface);
	log_bool("pico_face_tracker", pico_face_tracker);
	log_bool("discard_frame", discard_frame);
}

static XrViewConfigurationView scale_view(XrViewConfigurationView view, uint32_t width)
{
	double ratio = double(width) / view.recommendedImageRectWidth;
	view.recommendedImageRectWidth = width;
	view.recommendedImageRectHeight *= ratio;
	spdlog::info("Using panel size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	return view;
}

std::string hmd_traits::model_name() const
{
#ifdef __ANDROID__
	const auto manufacturer = get_property("ro.product.manufacturer").value_or("");
	const auto model = get_property("ro.product.model").value_or("");

	return manufacturer + " " + model;
#else
	return "Unknown headset";
#endif
}

XrViewConfigurationView hmd_traits::override_view(XrViewConfigurationView view) const
{
	// Standalone headsets tend to report a lower resolution
	// as the GPU can't handle full res.
	// Return the panel resolution instead.
	spdlog::debug("Recommended image size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	if (panel_width_override > 0)
		return scale_view(view, panel_width_override);
	return view;
}

std::pair<glm::vec3, glm::quat> hmd_traits::controller_offset(xr::spaces space) const
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

	std::string_view profile = controller_profile;

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
				return {{0, -0.006, -0.025}, glm::angleAxis(glm::radians(-14.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{-0.010, 0, 0.02}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.010, 0, 0.02}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "meta-quest-touch-pro")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, 0, 0}, glm::angleAxis(glm::radians(-20.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{0, 0, 0}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0, 0, 0}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "oculus-touch-v3")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.003, -0.047}, glm::angleAxis(glm::radians(-20.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{-0.006, 0, 0.015}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.006, 0, 0.015}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "pico-neo3")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.016, -0.05}, glm::angleAxis(glm::radians(-15.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
			case xr::spaces::aim_right:
				return {{0, 0, 0}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "pico-4" or profile == "pico-4u")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.02, -0.05}, glm::angleAxis(glm::radians(-30.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{-0.005, 0, 0.02}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.005, 0, 0.02}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "htc-vive-focus-3")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, 0, -0.03}, glm::angleAxis(glm::radians(-35.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
			case xr::spaces::aim_right:
				return {{0, 0, 0}, {1, 0, 0, 0}};

			default:
				break;
		}
	}
	else if (profile == "samsung-galaxyxr")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.01, -0.02}, glm::angleAxis(glm::radians(-20.f), glm::vec3{1, 0, 0})};

			case xr::spaces::aim_left:
				return {{-0.003, 0, 0.028}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.003, 0, 0.028}, {1, 0, 0, 0}};

			default:
				break;
		}
	}

	return {{0, 0, 0}, {1, 0, 0, 0}};
}
