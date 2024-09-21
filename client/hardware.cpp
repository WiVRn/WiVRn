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
	if (model == "Lynx-R1")
		return model::lynx_r1;

	if (manufacturer == "Pico")
	{
		if (model == "Pico Neo 3")
			return model::pico_neo_3;

		spdlog::info("manufacturer={}, model={}, device={} assuming Pico 4", manufacturer, model, device);
		return model::pico_4;
	}
	if (manufacturer == "HTC")
	{
		if (model == "VIVE Focus 3")
			return model::htc_vive_focus_3;

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
			return scale_view(view, 1832);
		case model::meta_quest_pro:
			return scale_view(view, 1800);
		case model::meta_quest_3:
			return scale_view(view, 2064);
		case model::pico_neo_3:
			return scale_view(view, 1832);
		case model::pico_4:
			return scale_view(view, 2160);
		case model::htc_vive_focus_3:
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
		case model::pico_neo_3:
		case model::pico_4:
		case model::htc_vive_focus_3:
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
		case feature::eye_gaze:
			switch (guess_model())
			{
				case model::oculus_quest:
				case model::oculus_quest_2:
				case model::meta_quest_pro:
				case model::meta_quest_3:
					return "com.oculus.permission.EYE_TRACKING";
				case model::pico_neo_3:
				case model::pico_4:
					return "com.picovr.permission.EYE_TRACKING";
				case model::htc_vive_focus_3:
				case model::htc_vive_xr_elite:
				case model::lynx_r1:
				case model::unknown:
					return nullptr;
			}
		case feature::face_tracking:
			switch (guess_model())
			{
				case model::oculus_quest:
				case model::oculus_quest_2:
				case model::meta_quest_pro:
				case model::meta_quest_3:
					return "com.oculus.permission.FACE_TRACKING";
				case model::pico_neo_3:
				case model::pico_4:
				case model::htc_vive_focus_3:
				case model::htc_vive_xr_elite:
				case model::lynx_r1:
				case model::unknown:
					return nullptr;
			}
	}
	__builtin_unreachable();
}
