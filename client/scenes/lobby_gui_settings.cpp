/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

// The settings screens are drawn procedurally: each page builds a list of `setting`
// descriptors that reference the existing configuration members, and render_settings()
// renders the matching ui:: widget for each. Gating is done by conditionally adding a
// descriptor; the disabled-but-shown case uses the `enabled` predicate. Descriptors are
// rebuilt every frame, so dynamic options and descriptions just work.

#define IMGUI_DEFINE_MATH_OPERATORS

#include "lobby.h"

#include "application.h"
#include "configuration.h"
#include "decoder/decoder.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "scenes/gui_common.h"
#include "utils/i18n.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/system.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <boost/locale.hpp>
#include <cmath>
#include <functional>
#include <locale>
#include <optional>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <tuple>
#include <vector>

namespace ui = wivrn::ui;

namespace
{
constexpr float control_w = ui::metrics::setting_control_width;

enum class ui_kind
{
	toggle,
	slider,
	segmented,
	combo,
};

// One declarative setting. Numeric controls (slider/segmented/combo) use get_int/set_int;
// segmented/combo indices map through `options`. Strings are owned so localized text stays
// valid. `enabled` greys the row out without hiding it.
struct setting
{
	const char * id; // stable imgui id
	std::string label;
	std::string description;
	ui_kind ui = ui_kind::toggle;

	std::function<bool()> get_bool;
	std::function<void(bool)> set_bool;

	std::function<int()> get_int;
	std::function<void(int)> set_int;
	int v_min = 0;
	int v_max = 0;
	std::string fmt;
	std::function<std::vector<std::string>()> options;
	std::string title; // combo modal title

	std::optional<bool> default_bool; // reset target for toggles
	std::optional<int> default_int;   // reset target for slider/segmented/combo

	std::function<bool()> enabled;
};

void render_settings(const char * card_id, const std::vector<setting> & list)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui::metrics::card_item_spacing);
	ui::begin_card(card_id);

	for (const auto & s: list)
	{
		const bool enabled = s.enabled ? s.enabled() : true;
		ImGui::BeginDisabled(not enabled);

		// stable storage for the reset defaults during this call
		bool def_b = s.default_bool.value_or(false);
		int def_i = s.default_int.value_or(0);
		const bool * dpb = s.default_bool ? &def_b : nullptr;
		const int * dpi = s.default_int ? &def_i : nullptr;

		const float w = s.ui == ui_kind::toggle ? wivrn::gui::toggle_width() + (s.default_bool ? ui::reset_slot_width() : 0) : control_w;
		const float label_bottom = ui::setting_label(s.label, s.description, w);

		switch (s.ui)
		{
			case ui_kind::toggle: {
				bool v = s.get_bool();
				if (ui::toggle(s.id, &v, dpb))
					s.set_bool(v);
				break;
			}
			case ui_kind::slider: {
				int v = s.get_int();
				if (ui::slider_int(s.id, &v, s.v_min, s.v_max, s.fmt.c_str(), {control_w, 0}, dpi))
					s.set_int(v);
				break;
			}
			case ui_kind::segmented: {
				const auto opts = s.options();
				int v = s.get_int();
				if (ui::segmented(s.id, opts, &v, {control_w, 0}, dpi))
					s.set_int(v);
				break;
			}
			case ui_kind::combo: {
				const auto opts = s.options();
				std::vector<ui::combo_item> items;
				for (const auto & o: opts)
					items.push_back({o.c_str()});
				int v = s.get_int();
				if (ui::combo(s.id, s.title, items, &v, control_w, dpi))
					s.set_int(v);
				break;
			}
		}

		ImGui::EndDisabled();

		// keep the row tall enough for a multi-line description and add breathing room
		// below. Reserve with Dummy (SetCursorPos would not grow the content size).
		const float pad = std::max(label_bottom - ImGui::GetCursorPosY(), 0.f) + ui::metrics::label_bottom_pad;
		ImGui::Dummy({0, pad});
	}

	ui::end_card();
	ImGui::PopStyleVar();
}
} // namespace

void scenes::lobby::gui_performance()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	if (instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) and not session.get_refresh_rates().empty())
	{
		const auto rates = session.get_refresh_rates();
		list.push_back({
		        .id = "##refresh",
		        .label = _("Refresh rate"),
		        .description = _("Select refresh rate based on measured application performance.\nMay cause flicker when a change happens."),
		        .ui = ui_kind::segmented,
		        .get_int = [&config, rates] {
			        for (size_t i = 0; i < rates.size(); ++i)
				        if (rates[i] == config.preferred_refresh_rate)
					        return int(i) + 1;
			        return 0; },
		        .set_int = [this, &config, rates](int v) {
			        if (v == 0)
			        {
				        config.preferred_refresh_rate = 0;
				        config.fps_divider = 1;
			        }
			        else
			        {
				        session.set_refresh_rate(rates[v - 1]);
				        config.preferred_refresh_rate = rates[v - 1];
			        }
			        config.save(); },
		        .options = [rates] {
			        std::vector<std::string> opts;
			        opts.push_back(_C("automatic refresh rate", "Auto"));
			        for (float r: rates)
				        opts.push_back(fmt::format("{}", int(r)));
			        return opts; },
		        .default_int = 0,
		});
	}

	{
		const auto width = stream_view.recommendedImageRectWidth;
		const auto height = stream_view.recommendedImageRectHeight;
		list.push_back({
		        .id = "##resolution",
		        .label = _("Render resolution"),
		        .description = fmt::format(_cF("render resolution", "Pixels rendered per eye ({}x{}). Lower to gain performance."), int(width * config.resolution_scale), int(height * config.resolution_scale)),
		        .ui = ui_kind::slider,
		        .get_int = [&config] { return int(std::lround(config.resolution_scale * 100)); },
		        .set_int = [&config](int v) { config.resolution_scale = v / 100.0; config.save(); },
		        .v_min = 50,
		        .v_max = config.extended_config ? 350 : 150,
		        .fmt = "%d%%",
		        .default_int = 100,
		});
	}

	list.push_back({
	        .id = "##foveation",
	        .label = _("Foveated encoding"),
	        .description = config.check_feature(feature::eye_gaze)
	                               ? _("Higher values focus image quality where you look at, improving latency, power efficiency and quality.")
	                               : _("Higher values focus image quality at the center, improving latency, power efficiency and quality."),
	        .ui = ui_kind::slider,
	        .get_int = [&config] { return int(std::lround((1 - config.get_stream_scale()) * 100)); },
	        .set_int = [&config](int v) {
		        if (not config.extended_config)
			        v = std::clamp(v, 30, 80);
		        config.set_stream_scale(1 - v * 0.01);
		        config.save(); },
	        .v_min = 0,
	        .v_max = 80,
	        .fmt = "%d%%",
	        .default_int = int(std::lround((1 - (config.check_feature(feature::eye_gaze) ? 0.3 : 0.5)) * 100)),
	});

	if (instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		list.push_back({
		        .id = "##spacewarp",
		        .label = _("Application SpaceWarp"),
		        .description = _("Renders at half the refresh rate and synthesises in-between frames. Managed automatically while the refresh rate is set to Auto."),
		        .ui = ui_kind::toggle,
		        .get_bool = [&config] { return config.fps_divider == 2; },
		        .set_bool = [&config](bool v) { config.fps_divider = v ? 2 : 1; config.save(); },
		        .default_bool = false,
		        .enabled = [&config] { return config.preferred_refresh_rate != 0; },
		});
	}

	if (instance.has_extension(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))
	{
		list.push_back({
		        .id = "##high_power",
		        .label = _("High power mode"),
		        .description = _("Increase power usage to allow higher resolution and refresh rate. Drains battery and runs hot."),
		        .ui = ui_kind::toggle,
		        .get_bool = [&config] { return config.high_power_mode; },
		        .set_bool = [&config](bool v) { config.high_power_mode = v; config.save(); },
		        .default_bool = true,
		});
	}

	ui::page_header(_S("Performance"), _S("Frame rate, resolution and power draw."));
	render_settings("##performance", list);
}

void scenes::lobby::gui_streaming()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	auto codec_name = [](std::optional<wivrn::video_codec> codec) -> std::string {
		if (not codec)
			return _C("Codec", "Automatic");
		switch (*codec)
		{
			case wivrn::h264:
				return _C("Codec", "H.264");
			case wivrn::h265:
				return _C("Codec", "HEVC (H.265)");
			case wivrn::av1:
				return _C("Codec", "AV1");
			case wivrn::raw:
				break;
		}
		return _C("Codec", "Automatic");
	};

	std::vector<wivrn::video_codec> codecs;
	for (auto c: wivrn::decoder::supported_codecs())
		if (c != wivrn::raw)
			codecs.push_back(c);

	list.push_back({
	        .id = "##codec",
	        .label = _("Video codec"),
	        .description = _("How video is compressed before it is sent to the headset."),
	        .ui = ui_kind::combo,
	        .get_int = [&config, codecs] {
		        if (config.codec)
			        for (size_t i = 0; i < codecs.size(); ++i)
				        if (codecs[i] == *config.codec)
					        return int(i) + 1;
		        return 0; },
	        .set_int = [&config, codecs](int v) {
		        config.codec = v == 0 ? std::nullopt : std::optional(codecs[v - 1]);
		        config.save(); },
	        .options = [codecs, codec_name] {
		        std::vector<std::string> opts;
		        opts.push_back(codec_name(std::nullopt));
		        for (auto c: codecs)
			        opts.push_back(codec_name(c));
		        return opts; },
	        .title = _("Video codec"),
	        .default_int = 0,
	});

	if (config.codec == wivrn::h265 or config.codec == wivrn::av1)
	{
		list.push_back({
		        .id = "##ten_bit",
		        .label = _("10-bit color"),
		        .description = _("Higher color precision, supported by HEVC and AV1."),
		        .ui = ui_kind::toggle,
		        .get_bool = [&config] { return config.bit_depth == 10; },
		        .set_bool = [&config](bool v) { config.bit_depth = v ? 10 : 8; config.save(); },
		        .default_bool = true,
		});
	}

	const int mb = 1'000'000;
	list.push_back({
	        .id = "##bitrate",
	        .label = _("Bitrate"),
	        .description = _("Video data rate sent to the headset."),
	        .ui = ui_kind::slider,
	        .get_int = [&config] { return int(config.bitrate_bps / mb); },
	        .set_int = [&config](int v) { config.bitrate_bps = uint32_t(v) * mb; config.save(); },
	        .v_min = 5,
	        .v_max = int(config.max_bitrate() / mb),
	        .fmt = "%d Mbit/s",
	        .default_int = 50,
	});

	list.push_back({
	        .id = "##stream_gui",
	        .label = _("In-stream window"),
	        .description = _("Enables the configuration window to be shown while the game is streaming.\nIf enabled, the window is activated by pressing both thumbsticks."),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.enable_stream_gui; },
	        .set_bool = [&config](bool v) { config.enable_stream_gui = v; config.save(); },
	        .default_bool = true,
	});

	ui::page_header(_S("Streaming"), _S("How video is encoded and sent to the headset."));
	render_settings("##streaming", list);
}

void scenes::lobby::gui_post_processing()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	if (application::get_openxr_post_processing_supported())
	{
		auto flag_name = [](XrCompositionLayerSettingsFlagsFB f) -> std::string {
			switch (f)
			{
				case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB:
				case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB:
					return _C("openxr_post_processing", "Normal");
				case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB:
				case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB:
					return _C("openxr_post_processing", "Quality");
				default:
					return _C("openxr_post_processing", "Disabled");
			}
		};

		auto flag_combo = [&](const char * id, std::string label, std::string desc, std::array<XrCompositionLayerSettingsFlagsFB, 3> flags, XrCompositionLayerSettingsFlagsFB configuration::openxr_post_processing_settings::* member) {
			std::string title = label;
			list.push_back({
			        .id = id,
			        .label = std::move(label),
			        .description = std::move(desc),
			        .ui = ui_kind::combo,
			        .get_int = [&config, flags, member] {
				        for (size_t i = 0; i < flags.size(); ++i)
					        if (config.openxr_post_processing.*member == flags[i])
						        return int(i);
				        return 0; },
			        .set_int = [&config, flags, member](int v) {
				        config.openxr_post_processing.*member = flags[v];
				        config.save(); },
			        .options = [flags, flag_name] {
				        std::vector<std::string> o;
				        for (auto f: flags)
					        o.push_back(flag_name(f));
				        return o; },
			        .title = std::move(title),
			        .default_int = 0,
			});
		};

		flag_combo("##supersampling", _("Supersampling"), _("Reduce flicker for high contrast edges.\nUseful when the input resolution is high compared to the headset display."), {0, XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB, XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB}, &configuration::openxr_post_processing_settings::super_sampling);
		flag_combo("##sharpening", _("Sharpening"), _("Improve clarity of high contrast edges and counteract blur.\nUseful when the input resolution is low compared to the headset display."), {0, XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB, XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB}, &configuration::openxr_post_processing_settings::sharpening);
	}

	ui::page_header(_S("Post-processing"), _S("OpenXR layer supersampling and sharpening."));
	render_settings("##post_processing", list);
}

void scenes::lobby::gui_audio()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	list.push_back({
	        .id = "##microphone",
	        .label = _("Microphone"),
	        .description = _("Stream the headset microphone to the PC."),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.check_feature(feature::microphone); },
	        .set_bool = [&config](bool v) { config.set_feature(feature::microphone, v); },
	        .default_bool = false,
	});

	list.push_back({
	        .id = "##unprocessed",
	        .label = _("Unprocessed audio"),
	        .description = _("Force disable audio filters, such as noise cancellation."),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.mic_unprocessed_audio; },
	        .set_bool = [&config](bool v) { config.mic_unprocessed_audio = v; config.save(); },
	        .default_bool = false,
	        .enabled = [&config] { return config.check_feature(feature::microphone); },
	});

	ui::page_header(_S("Audio"), _S("Microphone streamed to the PC."));
	render_settings("##audio", list);
}

void scenes::lobby::gui_devices()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	list.push_back({
	        .id = "##keyboard",
	        .label = _("Keyboard"),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.forward_keyboard; },
	        .set_bool = [&config](bool v) { config.forward_keyboard = v; config.save(); },
	        .default_bool = false,
	});

	list.push_back({
	        .id = "##mouse",
	        .label = _("Mouse"),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.forward_mouse; },
	        .set_bool = [&config](bool v) { config.forward_mouse = v; config.save(); },
	        .default_bool = false,
	});

	list.push_back({
	        .id = "##gamepad",
	        .label = _("Gamepad"),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.forward_gamepad; },
	        .set_bool = [&config](bool v) { config.forward_gamepad = v; config.save(); },
	        .default_bool = false,
	});

	ui::page_header(_S("Devices"), _S("Forward input devices to the PC."));
	render_settings("##devices", list);

	if (server_hid_forwarding == false and (config.forward_keyboard or config.forward_mouse or config.forward_gamepad))
		ui::chip(_("The server does not allow forwarded input devices"), ui::chip_style::warning);
}

void scenes::lobby::gui_tracking()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	auto feature_toggle = [&](const char * id, std::string label, std::string desc, feature f, bool def) {
		list.push_back({
		        .id = id,
		        .label = std::move(label),
		        .description = std::move(desc),
		        .ui = ui_kind::toggle,
		        .get_bool = [&config, f] { return config.check_feature(f); },
		        .set_bool = [&config, f](bool v) { config.set_feature(f, v); },
		        .default_bool = def,
		});
	};

	if (system.hand_tracking_supported())
		feature_toggle("##hand", _("Hand tracking"), _("Track your hands for input when controllers are down."), feature::hand_tracking, true);

	if (application::get_eye_gaze_supported())
		feature_toggle("##eye", _("Eye tracking"), _("Used by foveated encoding to focus quality where you look."), feature::eye_gaze, false);

	if (system.face_tracker_supported() != xr::face_tracker_type::none)
		feature_toggle("##face", _("Face tracking"), _("Stream facial expressions to the PC."), feature::face_tracking, false);

	const auto body_tracker = system.body_tracker_supported();
	if (body_tracker != xr::body_tracker_type::none)
		feature_toggle("##body",
		               _("Body tracking"),
		               body_tracker == xr::body_tracker_type::fb or body_tracker == xr::body_tracker_type::meta
		                       ? _("Requires 'Hand and body tracking' to be enabled in the Quest movement tracking settings, otherwise body data will be guessed from controller and headset positions.")
		                       : _("Stream body joint positions to the PC."),
		               feature::body_tracking,
		               false);

	ui::page_header(_S("Tracking"), _S("Body and input tracking sent to the PC."));
	render_settings("##tracking", list);

	if (body_tracker != xr::body_tracker_type::none)
		wivrn::gui::body_tracking_parts(system, *imgui_ctx, config);
}

void scenes::lobby::gui_system()
{
	auto & config = application::get_config();
	std::vector<setting> list;

	auto language_name = [](const std::locale & loc = std::locale()) {
		return boost::locale::pgettext("language selection, replace with the name of the language", "English", loc);
	};
	std::vector<std::tuple<std::string, std::locale, std::string>> languages;
	for (const auto & msg_info: get_locales())
	{
		std::string code = msg_info.language;
		if (not msg_info.country.empty())
			code += "_" + msg_info.country;
		std::locale loc(std::locale(), boost::locale::gnu_gettext::create_messages_facet<char>(msg_info));
		languages.emplace_back(language_name(loc), loc, std::move(code));
	}
	std::ranges::sort(languages, [](auto & l, auto & r) { return std::get<0>(l) < std::get<0>(r); });

	list.push_back({
	        .id = "##language",
	        .label = _("Language"),
	        .description = _("Interface language."),
	        .ui = ui_kind::combo,
	        .get_int = [&config, languages] {
		        if (not config.locale.empty())
			        for (size_t i = 0; i < languages.size(); ++i)
				        if (std::get<2>(languages[i]) == config.locale)
					        return int(i) + 1;
		        return 0; },
	        .set_int = [&config, languages](int v) {
		        config.locale = v == 0 ? "" : std::get<2>(languages[v - 1]);
		        config.save();
		        application::instance().load_locale(); },
	        .options = [languages] {
		        std::vector<std::string> opts;
		        opts.push_back(_C("language", "System language"));
		        for (const auto & [lang, loc, code]: languages)
			        opts.push_back(lang);
		        return opts; },
	        .title = _("Language"),
	        .default_int = 0,
	});

	list.push_back({
	        .id = "##extended",
	        .label = _("Extended configuration"),
	        .description = _("Allows unsafe configuration values, use at your own risk."),
	        .ui = ui_kind::toggle,
	        .get_bool = [&config] { return config.extended_config; },
	        .set_bool = [&config](bool v) { config.extended_config = v; config.save(); },
	        .default_bool = false,
	});

	ui::page_header(_S("System"), _S("Language and advanced options."));
	render_settings("##system", list);
}
