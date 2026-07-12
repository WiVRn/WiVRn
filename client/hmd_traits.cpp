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

#include "utils/overloaded.h"
#include "utils/strings.h"
#include "xr/to_string.h"

#include <boost/pfr.hpp>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

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

template <typename T>
std::optional<T> env(std::string_view name);

template <>
std::optional<std::string> env(std::string_view name)
{
#ifdef __ANDROID__
	auto android_sysprop_name = std::format("debug.wivrn.{}", name);
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

template <>
std::optional<uint32_t> env(std::string_view name)
{
	const auto value = env<std::string>(name);
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

template <>
std::optional<bool> env(std::string_view name)
{
	const auto value = env<std::string>(name);
	if (not value)
		return std::nullopt;
	if (strcmp(value->c_str(), "1") == 0 || strcasecmp(value->c_str(), "true") == 0 || strcasecmp(value->c_str(), "yes") == 0 || strcasecmp(value->c_str(), "on") == 0)
		return true;
	if (strcmp(value->c_str(), "0") == 0 || strcasecmp(value->c_str(), "false") == 0 || strcasecmp(value->c_str(), "no") == 0 || strcasecmp(value->c_str(), "off") == 0)
		return false;
	return std::nullopt;
}

template <>
std::optional<std::unordered_map<std::string, std::string>> env(std::string_view name)
{
	const auto values = env<std::string>(name);
	if (not values)
		return std::nullopt;

	std::unordered_map<std::string, std::string> map;
	for (auto value: utils::split(*values, ","))
	{
		if (auto pos = value.find('='); pos != std::string::npos)
			map.insert(std::make_pair(value.substr(0, pos), value.substr(pos + 1)));
	}

	return map;
}

template <>
std::optional<std::unordered_set<std::string>> env(std::string_view name)
{
	const auto values = env<std::string>(name);
	if (not values)
		return std::nullopt;

	return std::unordered_set<std::string>{std::from_range, utils::split(*values, ",")};
}

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

	else if (manufacturer == "Play For Dream" and model == "PFDM MR")
	{
		controller_profile = "yvr-touch-v2";
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
		// Fixed in XR elite firmware version 2.0
		bool need_htc_ray = true;
		const auto version = get_property("ro.product.version");
		if (version)
		{
			auto digits = utils::split(*version, ".");
			if (not digits.empty())
			{
				try
				{
					auto major = std::stoi(digits[0]);
					if (major >= 2 and model == "VIVE XR Series")
						need_htc_ray = false;
				}
				catch (...)
				{}
			}
		}
		if (need_htc_ray)
			override_shader = {
			        {"ray.frag", "ray_htc.frag"},
			        {"ray_skinned.vert", "ray_skinned_htc.vert"},
			        {"ray.vert", "ray_htc.vert"},
			};

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

	boost::pfr::for_each_field_with_name(
	        *this,
	        utils::overloaded{
	                [](std::string_view name, auto & field) {
		                if (auto val = env<std::remove_cvref_t<decltype(field)>>(name))
		                {
			                spdlog::info("\t{} override: {} -> {}", name, field, *val);
			                field = *val;
		                }
		                else
		                {
			                spdlog::info("\t{}: {}", name, field);
		                }
	                },
	                [](std::string_view name, XrVersion & field) {
		                if (auto val = env<std::string>(name))
		                {
			                XrVersion v;
			                if (val == "1.1")
				                v = XR_API_VERSION_1_1;
			                else if (val == "1.0")
				                v = XR_API_VERSION_1_0;
			                else
			                {
				                spdlog::warn("XrVersion {} not recognized", *val);
				                v = field;
			                }
			                spdlog::info("\t{} override: {} -> {}", name, xr::to_string(field), xr::to_string(v));
			                field = v;
		                }
		                else
		                {
			                spdlog::info("\t{}: {}", name, xr::to_string(field));
		                }
	                },
	                [](std::string_view name, std::unordered_map<std::string, std::string> & field) {
		                if (auto val = env<std::unordered_map<std::string, std::string>>(name))
		                {
			                for (const auto & [original, overridden]: *val)
				                field.insert_or_assign(original, overridden);
		                }
		                std::erase_if(field, [](const std::pair<std::string, std::string> & kv) { return kv.second == ""; });

		                for (const auto & [original, overridden]: field)
		                {
			                spdlog::info("\t{}: {} -> {}", name, original, overridden);
		                }
	                },

	                [](std::string_view name, std::unordered_set<std::string> & field) {
		                if (auto val = env<std::unordered_set<std::string>>(name))
		                {
			                for (const auto & i: *val)
			                {
				                if (i.starts_with("-"))
					                field.erase(i.substr(1));
				                else
					                field.insert(i);
			                }
		                }

		                for (const auto & i: field)
		                {
			                spdlog::info("\t{}: {}", name, i);
		                }
	                },
	                [](std::string_view, hmd_permissions) {},
	        });
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
	else if (profile == "pico-4")
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
	else if (profile == "pico-4u")
	{
		switch (space)
		{
			case xr::spaces::grip_left:
			case xr::spaces::grip_right:
				return {{0, -0.042, -0.042}, glm::angleAxis(glm::radians(-8.f), glm::vec3{1, 0, 0})};

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
				return {{0, 0, 0}, {1, 0, 0, 0}};

			case xr::spaces::aim_left:
			case xr::spaces::aim_right:
				return {{0, -0.045, 0.035}, {1, 0, 0, 0}};

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
