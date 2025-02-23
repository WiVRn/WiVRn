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

#include <cstdlib>
#include <glm/ext/quaternion_trigonometric.hpp>
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
		if (model == "Pico Neo 3")
			return model::pico_neo_3;

		if (model == "A9210")
			return model::pico_4s;

		spdlog::info("manufacturer={}, model={}, device={} assuming Pico 4", manufacturer, model, device);
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

	spdlog::info("Unknown model, manufacturer={}, model={}, device={}", manufacturer, model, device);
#endif
	return model::unknown;
}

model guess_model()
{
	static model m = guess_model_();
	return m;
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

XrViewConfigurationView override_view(XrViewConfigurationView view, model m)
{
	// Standalone headsets tend to report a lower resolution
	// as the GPU can't handle full res.
	// Return the panel resolution instead.
	spdlog::debug("Recommended image size: {}x{}", view.recommendedImageRectWidth, view.recommendedImageRectHeight);
	switch (m)
	{
		case model::oculus_quest:
			return scale_view(view, 1440);
		case model::oculus_quest_2:
		case model::meta_quest_3s:
			return scale_view(view, 1832);
		case model::meta_quest_pro:
			return scale_view(view, 1800);
		case model::meta_quest_3:
			return scale_view(view, 2064);
		case model::pico_neo_3:
			return scale_view(view, 1832);
		case model::pico_4:
		case model::pico_4s:
			return scale_view(view, 2160);
		case model::htc_vive_focus_3:
		case model::htc_vive_focus_vision:
			return scale_view(view, 2448);
		case model::htc_vive_xr_elite:
			return scale_view(view, 1920);
		case model::lynx_r1:
		case model::unknown:
			return view;
	}
	throw std::range_error("invalid model " + std::to_string((int)m));
}

bool need_srgb_conversion(model m)
{
	switch (m)
	{
		case model::lynx_r1:
			return false;
		case model::oculus_quest:
		case model::oculus_quest_2:
		case model::meta_quest_pro:
		case model::meta_quest_3:
		case model::meta_quest_3s:
		case model::pico_neo_3:
		case model::pico_4:
		case model::pico_4s:
		case model::htc_vive_focus_3:
		case model::htc_vive_focus_vision:
		case model::htc_vive_xr_elite:
		case model::unknown:
			return true;
	}
	throw std::range_error("invalid model " + std::to_string((int)m));
}

const char * permission_name(feature f)
{
	switch (f)
	{
		case feature::microphone:
			return "android.permission.RECORD_AUDIO";
		case feature::hand_tracking:
			return nullptr;
		case feature::eye_gaze:
			switch (guess_model())
			{
				case model::oculus_quest:
				case model::oculus_quest_2:
				case model::meta_quest_pro:
				case model::meta_quest_3:
				case model::meta_quest_3s:
					return "com.oculus.permission.EYE_TRACKING";
				case model::pico_neo_3:
				case model::pico_4:
				case model::pico_4s:
					return "com.picovr.permission.EYE_TRACKING";
				case model::htc_vive_focus_3:
				case model::htc_vive_focus_vision:
				case model::htc_vive_xr_elite:
				case model::lynx_r1:
				case model::unknown:
					return nullptr;
			}
			__builtin_unreachable();
		case feature::face_tracking:
			switch (guess_model())
			{
				case model::oculus_quest:
				case model::oculus_quest_2:
				case model::meta_quest_pro:
				case model::meta_quest_3:
				case model::meta_quest_3s:
					return "com.oculus.permission.FACE_TRACKING";
				case model::pico_neo_3:
				case model::pico_4:
				case model::pico_4s:
				case model::htc_vive_focus_3:
				case model::htc_vive_focus_vision:
				case model::htc_vive_xr_elite:
				case model::lynx_r1:
				case model::unknown:
					return nullptr;
			}
			__builtin_unreachable();
	}
	__builtin_unreachable();
}

std::string controller_name()
{
#ifndef __ANDROID__
	const char * controller = std::getenv("WIVRN_CONTROLLER");
	if (controller && strcmp(controller, ""))
		return controller;
#endif

	switch (guess_model())
	{
		case model::oculus_quest:
			return "oculus-touch-v2";
		case model::oculus_quest_2:
			return "oculus-touch-v3";
		case model::meta_quest_pro:
			return "meta-quest-touch-pro";
		case model::meta_quest_3:
		case model::meta_quest_3s:
			return "meta-quest-touch-plus";
		case model::pico_neo_3:
			return "pico-neo3";
		case model::pico_4:
			return "pico-4";
		case model::pico_4s:
			return "pico-4s";
		case model::htc_vive_focus_3:
		case model::htc_vive_focus_vision:
		case model::htc_vive_xr_elite:
			return "htc-vive-focus-3";
		case model::lynx_r1:
		case model::unknown:
			return "generic-trigger-squeeze";
	}

	__builtin_unreachable();
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
				return {{-0.010, 0, 0.025}, {1, 0, 0, 0}};

			case xr::spaces::aim_right:
				return {{0.010, 0, 0.025}, {1, 0, 0, 0}};

			default:
				break;
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
				return {{0, -0.030, -0.040}, glm::angleAxis(glm::radians(-35.f), glm::vec3{1, 0, 0})};

			default:
				break;
		}

	return {{0, 0, 0}, {1, 0, 0, 0}};
}

std::string controller_ray_model_name()
{
	switch (guess_model())
	{
		case model::htc_vive_focus_3:
		case model::htc_vive_focus_vision:
		case model::htc_vive_xr_elite:
			// XR Elite's runtime always assume alpha is unpremultiplied in the composition layers
			// Assume it's the same for all HTC headsets
			return "ray-htc.gltf";

		default:
			return "ray.gltf";
	}
}
