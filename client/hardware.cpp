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

#include <cstdlib>
#include <cstring>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include "utils/wrap_lambda.h"

#include <sys/system_properties.h>

static std::string get_property(const char * property)
{
	auto info = __system_property_find(property);
	std::string result;
	if (not info)
		return result;
	wrap_lambda cb = [&result](const char * name, const char * value, uint32_t serial) {
		result = value;
	};

	__system_property_read_callback(info, cb.userdata_first(), cb);
	return result;
}
#endif

static model guess_model_()
{
#ifdef __ANDROID__
	const auto device = get_property("ro.product.device");
	const auto manufacturer = get_property("ro.product.manufacturer");
	const auto model = get_property("ro.product.model");

	spdlog::info("Guessing HMD model from:");
	spdlog::info("    ro.product.device = \"{}\":", device);
	spdlog::info("    ro.product.manufacturer = \"{}\":", manufacturer);
	spdlog::info("    ro.product.model = \"{}\":", model);

	if (device == "monterey")
		return model::oculus_quest;
	if (device == "hollywood")
		return model::oculus_quest_2;
	if (device == "seacliff")
		return model::meta_quest_pro;
	if (device == "eureka")
		return model::meta_quest_3;
	if (device == "panther")
		return model::meta_quest_3s;
	if (model == "Lynx-R1")
		return model::lynx_r1;

	if (manufacturer == "Pico")
	{
		const auto pico_model = get_property("pxr.vendorhw.product.model");
		spdlog::info("    pxr.vendorhw.product.model = \"{}\":", pico_model);

		if (pico_model == "Pico Neo 3")
			return model::pico_neo_3;

		if (pico_model == "PICO 4")
			return model::pico_4;

		if (pico_model == "PICO 4 Ultra")
			return model::pico_4s;

		if (pico_model == "PICO 4 Pro")
			return model::pico_4_pro;

		if (pico_model == "PICO 4 Enterprise")
			return model::pico_4_enterprise;

		spdlog::info("manufacturer={}, model={}, device={}, pico_model={} assuming Pico 4", manufacturer, model, device, pico_model);
		return model::pico_4;
	}
	if (manufacturer == "HTC")
	{
		if (model == "VIVE Focus 3")
			return model::htc_vive_focus_3;

		if (model == "VIVE Focus Vision")
			return model::htc_vive_focus_vision;

		if (model == "VIVE XR Series")
			return model::htc_vive_xr_elite;
	}
	if (manufacturer == "samsung")
	{
		if (model == "SM-I610")
			return model::samsung_galaxy_xr;
	}

	spdlog::info("Unknown model, manufacturer={}, model={}, device={}", manufacturer, model, device);
#endif
	return model::unknown;
}

model guess_model()
{
	static model m = guess_model_();
	return m;
}

const char * permission_name_for_hmd(const hmd_traits & traits, const feature f)
{
	if (f == feature::microphone)
		return "android.permission.RECORD_AUDIO";

	if (!traits.permissions)
		return nullptr;

	return (*traits.permissions)[f];
}

static std::optional<uint32_t> env_u32(const char * env_name, const char * android_sysprop_name)
{
#ifdef __ANDROID__
	const std::string value = get_property(android_sysprop_name);
#else
	const char * env_value = std::getenv(env_name);
	const std::string value = (env_value != nullptr) ? std::string(env_value) : std::string();
#endif
	if (value.empty())
		return std::nullopt;
	char * end = nullptr;
	const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
	if (end == value.c_str() || *end != '\0')
		return std::nullopt;
	if (parsed > std::numeric_limits<uint32_t>::max())
		return std::nullopt;
	return static_cast<uint32_t>(parsed);
}

static std::optional<bool> env_bool(const char * env_name, const char * android_sysprop_name)
{
#ifdef __ANDROID__
	const std::string value = get_property(android_sysprop_name);
#else
	const char * env_value = std::getenv(env_name);
	const std::string value = (env_value != nullptr) ? std::string(env_value) : std::string();
#endif
	if (value.empty())
		return std::nullopt;
	if (strcmp(value.c_str(), "1") == 0 || strcasecmp(value.c_str(), "true") == 0 || strcasecmp(value.c_str(), "yes") == 0 || strcasecmp(value.c_str(), "on") == 0)
		return true;
	if (strcmp(value.c_str(), "0") == 0 || strcasecmp(value.c_str(), "false") == 0 || strcasecmp(value.c_str(), "no") == 0 || strcasecmp(value.c_str(), "off") == 0)
		return false;
	return std::nullopt;
}

static std::optional<std::string> env_string(const char * env_name, const char * android_sysprop_name)
{
#ifdef __ANDROID__
	const std::string value = get_property(android_sysprop_name);
	if (!value.empty())
		return value;
	return std::nullopt;
#else
	const char * value = std::getenv(env_name);
	if (value != nullptr)
		return std::string(value);
	return std::nullopt;
#endif
}

static const hmd_permissions permission_quest = [] {
	hmd_permissions permissions{};
	permissions[feature::eye_gaze] = "com.oculus.permission.EYE_TRACKING";
	permissions[feature::face_tracking] = "com.oculus.permission.FACE_TRACKING";
	return permissions;
}();
static const hmd_permissions permission_pico = [] {
	hmd_permissions permissions{};
	permissions[feature::eye_gaze] = "com.picovr.permission.EYE_TRACKING";
	permissions[feature::face_tracking] = "com.picovr.permission.FACE_TRACKING";
	return permissions;
}();
static const hmd_permissions permission_samsung = [] {
	hmd_permissions permissions{};
	permissions[feature::hand_tracking] = "android.permission.HAND_TRACKING";
	permissions[feature::eye_gaze] = "android.permission.EYE_TRACKING_FINE";
	permissions[feature::face_tracking] = "android.permission.FACE_TRACKING";
	return permissions;
}();
static const hmd_permissions permission_quest_body = [] {
	hmd_permissions permissions{};
	permissions[feature::eye_gaze] = "com.oculus.permission.EYE_TRACKING";
	permissions[feature::face_tracking] = "com.oculus.permission.FACE_TRACKING";
	permissions[feature::body_tracking] = "com.oculus.permission.BODY_TRACKING";
	return permissions;
}();

static hmd_traits get_hmd_traits(const model m)
{
	hmd_traits traits{};
	switch (m)
	{
		case model::oculus_quest:
			traits.panel_width_override = 1440;
			traits.controller_profile = "oculus-touch-v2";
			traits.permissions = &permission_quest;
			break;
		case model::oculus_quest_2:
			traits.panel_width_override = 1832;
			traits.controller_profile = "oculus-touch-v3";
			traits.permissions = &permission_quest;
			break;
		case model::meta_quest_pro:
			traits.panel_width_override = 1800;
			traits.controller_profile = "meta-quest-touch-pro";
			traits.permissions = &permission_quest;
			break;
		case model::meta_quest_3:
			traits.controller_profile = "meta-quest-touch-plus";
			traits.panel_width_override = 2064;
			traits.permissions = &permission_quest_body;
			break;
		case model::meta_quest_3s:
			traits.panel_width_override = 1832;
			traits.controller_profile = "meta-quest-touch-plus";
			traits.permissions = &permission_quest_body;
			break;
		case model::pico_neo_3:
			traits.panel_width_override = 1832;
			traits.controller_profile = "pico-neo3";
			traits.permissions = &permission_pico;
			break;
		case model::pico_4:
		case model::pico_4_pro:
		case model::pico_4_enterprise:
			traits.panel_width_override = 2160;
			traits.controller_profile = "pico-4";
			traits.permissions = &permission_pico;
			break;
		case model::pico_4s:
			traits.panel_width_override = 2160;
			traits.controller_profile = "pico-4u";
			traits.permissions = &permission_pico;
			break;
		case model::htc_vive_focus_3:
			traits.panel_width_override = 2448;
			traits.max_openxr_api_version = XR_API_VERSION_1_0;
			traits.controller_profile = "htc-vive-focus-3";
			traits.controller_ray_model = "assets://ray-htc.glb";
			break;
		case model::htc_vive_xr_elite:
			traits.panel_width_override = 1920;
			traits.max_openxr_api_version = XR_API_VERSION_1_0;
			traits.controller_profile = "htc-vive-focus-3";
			traits.controller_ray_model = "assets://ray-htc.glb";
			break;
		case model::htc_vive_focus_vision:
			traits.panel_width_override = 2448;
			traits.max_openxr_api_version = XR_API_VERSION_1_0;
			traits.controller_profile = "htc-vive-focus-3";
			traits.controller_ray_model = "assets://ray-htc.glb";
			break;
		case model::lynx_r1:
			traits.needs_srgb_conversion = false;
			break;
		case model::samsung_galaxy_xr:
			traits.panel_width_override = 3552;
			traits.controller_profile = "samsung-galaxyxr";
			traits.permissions = &permission_samsung;
			break;
		case model::unknown:
			break;
	}

	return traits;
}

namespace
{
std::optional<hmd_traits> g_hmd_traits;
std::string g_controller_override_storage;
} // namespace

void initialize_runtime_hmd_traits()
{
	if (g_hmd_traits.has_value())
		return;

	hmd_traits q = get_hmd_traits(guess_model());
	const std::optional<uint32_t> panel_width = env_u32("WIVRN_QUIRK_PANEL_WIDTH", "debug.wivrn.quirk_panel_width");
	const bool had_panel_override = panel_width.has_value();
	q.panel_width_override = panel_width.value_or(q.panel_width_override);
	const std::optional<bool> disable_openxr_1_1_override = env_bool("WIVRN_QUIRK_DISABLE_OPENXR_1_1", "debug.wivrn.quirk_disable_openxr_1_1");
	const bool disable_openxr_1_1 = disable_openxr_1_1_override.value_or(q.max_openxr_api_version < XR_API_VERSION_1_1);
	q.max_openxr_api_version = disable_openxr_1_1 ? XR_API_VERSION_1_0 : XR_API_VERSION_1_1;
	const std::optional<bool> srgb_override = env_bool("WIVRN_QUIRK_SRGB_CONVERSION", "debug.wivrn.quirk_srgb_conversion");
	const bool srgb = srgb_override.value_or(q.needs_srgb_conversion);
	const bool had_srgb_override = srgb_override.has_value();
	q.needs_srgb_conversion = srgb;
	g_controller_override_storage = env_string("WIVRN_CONTROLLER", "debug.wivrn.controller").value_or(std::string(q.controller_profile));
	const bool had_controller_override = g_controller_override_storage != q.controller_profile;
	q.controller_profile = g_controller_override_storage.c_str();
	spdlog::info("Initialized HMD traits: profile={}, ray_model={}, panel_width_override={}, max_openxr_api={}, needs_srgb_conversion={}",
	             q.controller_profile,
	             q.controller_ray_model,
	             q.panel_width_override,
	             xr::to_string(q.max_openxr_api_version),
	             q.needs_srgb_conversion);
	spdlog::info("HMD trait overrides: panel_width={}, disable_openxr_1_1={}, srgb_conversion={}, controller_profile={}",
	             had_panel_override,
	             disable_openxr_1_1_override.has_value(),
	             had_srgb_override,
	             had_controller_override);
	g_hmd_traits = q;
}

const hmd_traits & runtime_hmd_traits()
{
	if (!g_hmd_traits.has_value())
	{
		spdlog::critical("runtime_hmd_traits() accessed before initialize_runtime_hmd_traits(); falling back to lazy initialization. This is a bug!");
		initialize_runtime_hmd_traits();
	}
	return *g_hmd_traits;
}

std::string model_name()
{
#ifdef __ANDROID__
	const auto manufacturer = get_property("ro.product.manufacturer");
	const auto model = get_property("ro.product.model");

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

XrViewConfigurationView override_view_for_hmd(const hmd_traits & traits, XrViewConfigurationView view)
{
	// Standalone headsets tend to report a lower resolution
	// as the GPU can't handle full res.
	// Return the panel resolution instead.
	spdlog::debug("Recommended image size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	if (traits.panel_width_override > 0)
		return scale_view(view, traits.panel_width_override);
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
