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

model guess_model()
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
	if (device == "TBD")
		return model::htc_vive_focus_3;

	if (manufacturer == "Pico")
	{
		if (model == "Pico Neo 3")
			return model::pico_neo_3;

		spdlog::info("manufacturer={}, model={}, device={} assuming Pico 4", manufacturer, model, device);
		return model::pico_4;
	}

	spdlog::info("Unknown model, manufacturer={}, model={}, device={}", manufacturer, model, device);
#endif
	return model::unknown;
}

XrViewConfigurationView override_view(XrViewConfigurationView view, model m)
{
	// Standalone headsets tend to report a lower resolution
	// as the GPU can't handle full res.
	// Return the panel resolution instead.
	switch (m)
	{
		case model::oculus_quest:
			spdlog::info("Using panel resolution 1440x1600 for Quest");
			view.recommendedImageRectWidth = 1440;
			view.recommendedImageRectHeight = 1600;
			return view;
		case model::oculus_quest_2:
			spdlog::info("Using panel resolution 1832x1920 for Quest 2");
			view.recommendedImageRectWidth = 1832;
			view.recommendedImageRectHeight = 1920;
			return view;
		case model::meta_quest_pro:
			spdlog::info("Using panel resolution 1800x1920 for Quest pro");
			view.recommendedImageRectWidth = 1800;
			view.recommendedImageRectHeight = 1920;
			return view;
		case model::meta_quest_3:
			spdlog::info("Using panel resolution 2064x2208 for Quest 3");
			view.recommendedImageRectWidth = 2064;
			view.recommendedImageRectHeight = 2208;
			return view;
		case model::pico_neo_3:
			spdlog::info("Using panel resolution 1832x1920 for Pico Neo 3");
			view.recommendedImageRectWidth = 1832;
			view.recommendedImageRectHeight = 1920;
			return view;
		case model::pico_4:
			spdlog::info("Using panel resolution 2160x2160 for Pico 4");
			view.recommendedImageRectWidth = 2160;
			view.recommendedImageRectHeight = 2160;
			return view;
		case model::htc_vive_focus_3:
			spdlog::info("Using panel resolution 2448x2448 for HTC Vive Focus 3");
			view.recommendedImageRectWidth = 2448;
			view.recommendedImageRectHeight = 2448;
			return view;
		case model::unknown:
			return view;
	}
	throw std::range_error("invalid model " + std::to_string((int)m));
}
