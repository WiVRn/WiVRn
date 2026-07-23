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

#include <glm/ext/quaternion_common.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS

#include "stream.h"

#include "application.h"
#include "configuration.h"
#include "constants.h"
#include "gui_common.h"
#include "gui_settings.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "utils/i18n.h"
#include "utils/ranges.h"
#include <IconsFontAwesome6.h>
#include <chrono>
#include <cmath>
#include <glm/ext.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <limits>
#include <ranges>
#include <spdlog/spdlog.h>
#include <openxr/openxr.h>

namespace
{
float compute_plot_max_value(float * data, int count, ptrdiff_t stride)
{
	float max = 0;
	uintptr_t ptr = (uintptr_t)data;
	for (int i = 0; i < count; i++)
	{
		max = std::max(max, *(float *)(ptr + i * stride));
	}

	// First power of 10 less than the max
	float x = pow(10, floor(log10(max)));
	return ceil(max / x) * x;
}

std::pair<float, std::string> compute_plot_unit(float max_value)
{
	if (max_value > 1e9)
		return {1e-9, "G"};
	if (max_value > 1e6)
		return {1e-6, "M"};
	if (max_value > 1e3)
		return {1e-3, "k"};
	if (max_value > 1)
		return {1, ""};
	if (max_value > 1e-3)
		return {1e3, "m"};
	if (max_value > 1e-6)
		return {1e6, "u"};
	return {1e9, "n"};
}

struct getter_data
{
	uintptr_t data;
	int stride;
	float multiplier;
};

ImPlotPoint getter(int index, void * data_)
{
	getter_data & data = *(getter_data *)data_;

	return ImPlotPoint(index, *(float *)(data.data + index * data.stride) * data.multiplier);
}
} // namespace

void scenes::stream::accumulate_metrics(XrTime predicted_display_time, const std::array<std::shared_ptr<shard_accumulator::blit_handle>, view_count + 1> & blit_handles, const gpu_timestamps & timestamps)
{
	uint64_t rx = network_session->bytes_received();
	uint64_t tx = network_session->bytes_sent();

	float dt = (predicted_display_time - last_metric_time) * 1e-9f;

	// Sometimes the render function can be called with almost the same predicted_display_time,
	// which can cause issues with the bandwidth estimation.
	if (dt < 0.001f)
		return;

	bandwidth_rx = 0.8 * bandwidth_rx + 0.2 * float(rx - bytes_received) / dt;
	bandwidth_tx = 0.8 * bandwidth_tx + 0.2 * float(tx - bytes_sent) / dt;

	// Filter more aggressively for the compact view
	compact_bandwidth_rx = 0.99 * compact_bandwidth_rx + 0.01 * float(rx - bytes_received) / dt;
	compact_bandwidth_tx = 0.99 * compact_bandwidth_tx + 0.01 * float(tx - bytes_sent) / dt;
	compact_cpu_time = 0.99 * compact_cpu_time + 0.01 * application::get_cpu_time().count() * 1e-9f;
	compact_gpu_time = 0.99 * compact_gpu_time + 0.01 * timestamps.gpu_time;

	last_metric_time = predicted_display_time;
	bytes_received = rx;
	bytes_sent = tx;

	*(gpu_timestamps *)&global_metrics[metrics_offset] = timestamps;
	global_metrics[metrics_offset].cpu_time = application::get_cpu_time().count() * 1e-9f;
	global_metrics[metrics_offset].bandwidth_rx = bandwidth_rx * 8;
	global_metrics[metrics_offset].bandwidth_tx = bandwidth_tx * 8;

	std::vector<shard_accumulator::blit_handle *> active_handles;
	active_handles.reserve(blit_handles.size());
	for (const auto & h: blit_handles)
	{
		if (h)
			active_handles.push_back(h.get());
	}

	if (decoder_metrics.size() != active_handles.size())
		decoder_metrics.resize(active_handles.size());

	auto min_encode_begin = std::numeric_limits<decltype(active_handles[0]->feedback.encode_begin)>::max();
	for (const auto & bh: active_handles)
	{
		if (bh)
			min_encode_begin = std::min(min_encode_begin, bh->feedback.encode_begin);
	}

	for (auto && [metrics, bh]: std::views::zip(decoder_metrics, active_handles))
	{
		if (metrics.size() != global_metrics.size())
			metrics.resize(global_metrics.size());
		if (not bh)
			continue;

		// clang-format off
		metrics[metrics_offset] = bh ? decoder_metric{
			.encode_begin          = (bh->feedback.encode_begin          - min_encode_begin) * 1e-9f,
			.encode_end            = (bh->feedback.encode_end            - min_encode_begin) * 1e-9f,
			.send_begin            = (bh->feedback.send_begin            - min_encode_begin) * 1e-9f,
			.send_end              = (bh->feedback.send_end              - min_encode_begin) * 1e-9f,
			.received_first_packet = (bh->feedback.received_first_packet - min_encode_begin) * 1e-9f,
			.received_last_packet  = (bh->feedback.received_last_packet  - min_encode_begin) * 1e-9f,
			.sent_to_decoder       = (bh->feedback.sent_to_decoder       - min_encode_begin) * 1e-9f,
			.received_from_decoder = (bh->feedback.received_from_decoder - min_encode_begin) * 1e-9f,
			.blitted               = (bh->feedback.blitted               - min_encode_begin) * 1e-9f,
			.displayed             = (bh->feedback.displayed             - min_encode_begin) * 1e-9f,
			.predicted_display     = (bh->view_info.display_time         - min_encode_begin) * 1e-9f,
		}: decoder_metric{};
		// clang-format on
	}

	metrics_offset = (metrics_offset + 1) % global_metrics.size();
}

void scenes::stream::gui_performance_metrics()
{
	const ImGuiStyle & style = ImGui::GetStyle();

	ImVec2 window_size = ImGui::GetWindowSize() - ImVec2(2, 2) * style.WindowPadding;

	const std::array plots = {
	        // clang-format off
	        plot(_("CPU time"), {{"",          &global_metric::cpu_time}},     "s"),

	        plot(_("GPU time"), {{_("Defoveate"), &global_metric::gpu_time}},  "s"),

	        plot(_("Network"), {{_("Download"),  &global_metric::bandwidth_rx},
	                            {_("Upload"),    &global_metric::bandwidth_tx}}, "bit/s"),
	        // clang-format on
	};

	int n_plots = plots.size() + decoder_metrics.size();
	axis_scale.resize(n_plots);

	int n_cols = 2;
	int n_rows = ceil((float)n_plots / n_cols);

	ImVec2 plot_size = ImVec2(
	        window_size.x / n_cols - style.ItemSpacing.x * (n_cols - 1) / n_cols,
	        (window_size.y - 2 * ImGui::GetCurrentContext()->FontSize - 2 * style.ItemSpacing.y) / n_rows - style.ItemSpacing.y * (n_rows - 1) / n_rows);

	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(32, 32, 32, 64));
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, IM_COL32(0, 0, 0, 0));

	int n = 0;
	for (const auto & [title, subplots, unit]: plots)
	{
		if (ImPlot::BeginPlot(title.c_str(), plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
		{
			float min_v = 0;
			float max_v = 0;
			for (const auto & [subtitle, data]: subplots)
			{
				max_v = std::max(max_v, compute_plot_max_value(&(global_metrics.data()->*data), global_metrics.size(), sizeof(global_metric)));
			}
			auto [multiplier, prefix] = compute_plot_unit(max_v);

			if (axis_scale[n] == 0 || std::isnan(axis_scale[n]))
				axis_scale[n] = max_v;
			else
				axis_scale[n] = 0.99 * axis_scale[n] + 0.01 * max_v;

			auto color = ImPlot::GetColormapColor(n);

			std::string title_with_units = std::string(title) + " [" + prefix + unit + "]";
			ImPlot::SetupAxes(nullptr, title_with_units.c_str(), ImPlotAxisFlags_NoDecorations, 0);
			ImPlot::SetupAxesLimits(0, global_metrics.size() - 1, min_v * multiplier, axis_scale[n] * multiplier, ImGuiCond_Always);
			ImPlot::SetNextLineStyle(color);
			ImPlot::SetNextFillStyle(color, 0.25);

			for (const auto & [subtitle, data]: subplots)
			{
				getter_data gdata{
				        .data = (uintptr_t)&(global_metrics.data()->*data),
				        .stride = sizeof(global_metric),
				        .multiplier = multiplier,
				};
				ImPlot::PlotLineG(subtitle.c_str(), getter, &gdata, global_metrics.size(), ImPlotLineFlags_Shaded);

				double x[] = {double(metrics_offset), double(metrics_offset)};
				double y[] = {0, axis_scale[n] * multiplier};
				ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 1));
				ImPlot::PlotLine("", x, y, 2);
			}
			ImPlot::EndPlot();
		}

		if (++n % n_cols != 0)
			ImGui::SameLine();
	}

	for (auto && [index, metrics]: utils::enumerate(decoder_metrics))
	{
		std::string title = fmt::format(_F("Decoder {}"), std::to_string(index));
		if (ImPlot::BeginPlot(title.c_str(), plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
		{
			float min_v = 0;
			float max_v = compute_plot_max_value(&(metrics.data()->displayed), metrics.size(), sizeof(decoder_metric));
			// auto [ multiplier, prefix ] = compute_plot_unit(max_v);

			if (axis_scale[n] == 0)
				axis_scale[n] = max_v;
			else
				axis_scale[n] = 0.99 * axis_scale[n] + 0.01 * max_v;

			std::string title_with_units = _("Timings [ms]");
			ImPlot::SetupAxes(nullptr, title_with_units.c_str(), ImPlotAxisFlags_NoDecorations, 0);
			ImPlot::SetupAxesLimits(0, metrics.size() - 1, min_v * 1e3f, axis_scale[n] * 1e3f, ImGuiCond_Always);

			getter_data getter_encode_begin{
			        .data = (uintptr_t)&(metrics.data()->encode_begin),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_encode_end{
			        .data = (uintptr_t)&(metrics.data()->encode_end),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_send_begin{
			        .data = (uintptr_t)&(metrics.data()->send_begin),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_send_end{
			        .data = (uintptr_t)&(metrics.data()->send_end),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_first_packet{
			        .data = (uintptr_t)&(metrics.data()->received_first_packet),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_last_packet{
			        .data = (uintptr_t)&(metrics.data()->received_last_packet),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_sent_to_decoder{
			        .data = (uintptr_t)&(metrics.data()->sent_to_decoder),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_from_decoder{
			        .data = (uintptr_t)&(metrics.data()->received_from_decoder),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_blitted{
			        .data = (uintptr_t)&(metrics.data()->blitted),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_displayed{
			        .data = (uintptr_t)&(metrics.data()->displayed),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_predicted{
			        .data = (uintptr_t)&(metrics.data()->predicted_display),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			// clang-format off
			ImPlot::PlotShadedG(_S("Encode"),  getter, &getter_encode_begin,          getter, &getter_encode_end,            metrics.size());
			ImPlot::PlotShadedG(_S("Send"),    getter, &getter_send_begin,            getter, &getter_send_end,              metrics.size());
			ImPlot::PlotShadedG(_S("Receive"), getter, &getter_received_first_packet, getter, &getter_received_last_packet,  metrics.size());
			ImPlot::PlotShadedG(_S("Decode"),  getter, &getter_sent_to_decoder,       getter, &getter_received_from_decoder, metrics.size());
			ImPlot::PlotLineG(_S("Blitted"),   getter, &getter_blitted,                                                      metrics.size());
			ImPlot::PlotLineG(_S("Displayed"), getter, &getter_displayed,                                                    metrics.size());
			ImPlot::PlotLineG(_S("Predicted"), getter, &getter_predicted,                                                    metrics.size());
			// clang-format on

			double x[] = {double(metrics_offset), double(metrics_offset)};
			double y[] = {0, 1e9};
			ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 1));
			ImPlot::PlotLine("", x, y, 2);

			ImPlot::EndPlot();
		}

		if (++n % n_cols != 0)
			ImGui::SameLine();
	}

	ImPlot::PopStyleColor(5);
	{
		ImGui::TextUnformatted(
		        fmt::format(
		                _F("Estimated motion to photons latency: {}ms"),
		                tracking_control.lock()->motions_to_photons / 1'000'000)
		                .c_str());

		if (is_gui_interactable())
			ImGui::Text("%s", _S("Press the grip button to move the window"));
		else
			ImGui::Text("%s", _S("Press both thumbsticks to display the WiVRn window"));
	}
}

void scenes::stream::gui_compact_view()
{
	const auto & metrics = global_metrics[(metrics_offset + global_metrics.size() - 1) % global_metrics.size()];

	if (ImGui::BeginTable("metrics", 2))
	{
		auto f = [&](const char * label, float value, const char * unit) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%s", label);
			ImGui::TableNextColumn();
			ImGui::Text("%.1f %s", value, unit);
		};

		f(_S("Download"), 8 * compact_bandwidth_rx * 1e-6, "Mbit/s");
		f(_S("Upload"), 8 * compact_bandwidth_tx * 1e-6, "Mbit/s");
		f(_S("CPU time"), compact_cpu_time * 1000, "ms");
		f(_S("GPU time"), compact_gpu_time * 1000, "ms");
		f(_S("Motion to photon latency"),
		  tracking_control.lock()->motions_to_photons / 1'000'000.f,
		  "ms");
		ImGui::EndTable();
	}
}

static void send_settings_changed_packet(xr::session & session, wivrn_session * network, const configuration & config)
{
	network->send_control(
	        from_headset::settings_changed{
	                .preferred_refresh_rate = config.preferred_refresh_rate,
	                .minimum_refresh_rate = config.minimum_refresh_rate.value_or(0),
	                .fps_divider = config.fps_divider,
	                .bitrate_bps = config.bitrate_bps,
	                .mirror_gamepad = config.forward_gamepad,
	                .enabled_body_parts = config.body_part_mask,
	        });
}

void scenes::stream::gui_settings(float)
{
	// same pages as the lobby, with in_game enabling the in-stream controls
	wivrn::gui::settings_context ctx{
	        .config = application::get_config(),
	        .instance = instance,
	        .session = session,
	        .system = system,
	        .imgui_ctx = *imgui_ctx,
	        .recommended_width = width,
	        .recommended_height = height,
	        .in_game = true,
	        .on_streaming_changed = [this] { send_settings_changed_packet(session, network_session.get(), application::get_config()); },
	        .enter_bitrate_adjust = [this] { next_gui_status = stream_tab::bitrate_settings; },
	        .enter_foveation_adjust = [this] { next_gui_status = stream_tab::foveation_settings; },
	        .on_foveation_override_changed = [this] {
		        const auto & config = application::get_config();
		        override_foveation_enable = config.override_foveation_enable;
		        override_foveation_pitch = config.override_foveation_pitch;
		        override_foveation_distance = config.override_foveation_distance;
		        network_session->send_control(from_headset::override_foveation_center{
		                .enabled = override_foveation_enable,
		                .pitch = override_foveation_pitch,
		                .distance = override_foveation_distance,
		        }); },
	};

	switch (current_settings_page)
	{
		case settings_page::performance:
			wivrn::gui::settings_performance(ctx);
			break;
		case settings_page::streaming:
			wivrn::gui::settings_streaming(ctx);
			break;
		case settings_page::post_processing:
			wivrn::gui::settings_post_processing(ctx);
			break;
		case settings_page::audio:
			wivrn::gui::settings_audio(ctx);
			break;
		case settings_page::tracking:
			if (wivrn::gui::settings_tracking(ctx))
				send_settings_changed_packet(session, network_session.get(), ctx.config);
			break;
		case settings_page::system:
			wivrn::gui::settings_system(ctx);
			break;
	}
}

void scenes::stream::gui_bitrate_settings(float predicted_display_period)
{
	auto & config = application::get_config();
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	ImGui::Text("%s", _S("Use the right thumbstick to adjust the bitrate"));
	ImGui::Text("%s", _S("Press A to go back"));
	ImGui::Text("%s", fmt::format(_F("Bitrate: {}Mbit/s"), config.bitrate_bps / 1'000'000).c_str());
	ImGui::PopFont();

	// Maximum speed of 20Mbit/s
	float delta = application::read_action_float(settings_adjust).value_or(std::pair{0, 0}).second * 20'000'000.f * predicted_display_period;

	config.bitrate_bps = std::clamp(config.bitrate_bps + static_cast<int32_t>(delta), 1'000'000u, config.max_bitrate());

	bool ok = application::read_action_bool(foveation_ok).value_or(std::pair{0, false}).second;

	if (ok)
	{
		config.save();
		next_gui_status = stream_tab::settings;
	}

	send_settings_changed_packet(session, network_session.get(), application::get_config());
}

void scenes::stream::gui_foveation_settings(float predicted_display_period)
{
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	ImGui::Text("%s", _S("Use the thumbsticks to move the foveation center"));
	ImGui::Text("%s", _S("Press A to save or B to cancel"));
	ImGui::Text("%s", fmt::format(_F("Height {:.1f} deg"), -override_foveation_pitch * 180 / M_PI).c_str());
	ImGui::Text("%s", fmt::format(_F("Distance {:.2f} m"), override_foveation_distance).c_str());
	ImGui::PopFont();

	// Maximum speed 1 rad/s
	float delta_pitch = application::read_action_float(settings_adjust).value_or(std::pair{0, 0}).second * predicted_display_period;

	// Maximum speed 2m/s @ 1m
	float delta_distance = std::pow(constants::stream::gui_max_foveation_speed, application::read_action_float(foveation_distance).value_or(std::pair{0, 0}).second * predicted_display_period);

	override_foveation_pitch = std::clamp<float>(override_foveation_pitch + delta_pitch, constants::stream::gui_min_foveation_pitch, constants::stream::gui_max_foveation_pitch);
	override_foveation_distance = std::clamp<float>(override_foveation_distance * delta_distance, constants::stream::gui_min_foveation_distance, constants::stream::gui_max_foveation_distance);

	bool ok = application::read_action_bool(foveation_ok).value_or(std::pair{0, false}).second;
	bool cancel = application::read_action_bool(foveation_cancel).value_or(std::pair{0, false}).second;

	if (ok)
	{
		next_gui_status = stream_tab::settings;

		// Save settings
		auto & config = application::get_config();
		config.override_foveation_enable = true;
		config.override_foveation_pitch = override_foveation_pitch;
		config.override_foveation_distance = override_foveation_distance;
		config.save();
	}
	else if (cancel)
	{
		next_gui_status = stream_tab::settings;

		// Restore settings
		const auto & config = application::get_config();
		override_foveation_enable = config.override_foveation_enable;
		override_foveation_pitch = config.override_foveation_pitch;
		override_foveation_distance = config.override_foveation_distance;
	}

	network_session->send_control(from_headset::override_foveation_center{
	        .enabled = override_foveation_enable,
	        .pitch = override_foveation_pitch,
	        .distance = override_foveation_distance,
	});
}

void scenes::stream::gui_applications()
{
	auto now = instance.now();
	if (now - running_application_req > 1'000'000'000)
	{
		running_application_req = now;
		network_session->send_control(from_headset::get_running_applications{});
	}

	wivrn::ui::page_header(_S("Applications"), _S("Running XR applications on the server."));

	auto apps = running_applications.lock();
	std::ranges::sort(apps->applications, [](auto & l, auto & r) {
		if (l.overlay == r.overlay)
			return false;
		return r.overlay;
	});

	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float ctrl_h = ImGui::GetFrameHeight() * wivrn::ui::metrics::control_height;
	const std::string stop_label = wivrn::ui::icon_label(ICON_FA_XMARK, _("Stop"));
	const float stop_w = wivrn::ui::button_width(stop_label);
	const std::string active_label = wivrn::ui::icon_label(ICON_FA_CIRCLE_CHECK, _("Active"));
	const ImVec2 active_sz = wivrn::ui::chip_size(active_label);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, wivrn::ui::metrics::card_item_spacing);
	wivrn::ui::begin_list_card("##running");
	{
		if (apps->applications.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, wivrn::ui::current().text_muted);
			ImGui::TextUnformatted(_S("No XR application is currently running."));
			ImGui::PopStyleColor();
		}

		bool overlay = false;
		bool first = true;
		for (const auto & app: apps->applications)
		{
			if (app.overlay and not overlay)
			{
				overlay = true;
				ImGui::PushStyleColor(ImGuiCol_Text, wivrn::ui::current().text_muted);
				ImGui::TextUnformatted(_S("Overlays"));
				ImGui::PopStyleColor();
				first = true;
			}
			ImGui::PushID(static_cast<int>(app.id));
			if (not first)
				wivrn::ui::row_separator();
			first = false;

			// overlays and the active app aren't selectable, only their stop button acts
			const bool interactive = not(app.active or app.overlay);
			const float trailing = stop_w + (app.active ? gap + active_sz.x : 0) + wivrn::ui::metrics::list_row_pad;
			const auto row = wivrn::ui::begin_list_row("##row", ICON_FA_CUBE, 0, app.name, {}, app.active, trailing, 0, false, interactive);
			float x = row.max.x;

			ImGui::SetCursorScreenPos(row.trailing(x, {stop_w, ctrl_h}));
			if (wivrn::ui::button(stop_label, wivrn::ui::button_style::danger, {stop_w, 0}))
				network_session->send_control(from_headset::stop_application{.id = app.id});
			if (ImGui::IsItemHovered())
				imgui_ctx->tooltip(_S("Request to quit, may be ignored by the application"));
			x -= stop_w + gap;

			if (app.active)
			{
				ImGui::SetCursorScreenPos(row.trailing(x, active_sz));
				wivrn::ui::chip(active_label, wivrn::ui::chip_style::success);
			}

			if (row.clicked and interactive)
				network_session->send_control(from_headset::set_active_application{.id = app.id});

			wivrn::ui::end_list_row();
			ImGui::PopID();
		}
	}
	wivrn::ui::end_card();
	ImGui::PopStyleVar();
}

void scenes::stream::gui_toasts()
{
	auto toast = gui_toast.lock();

	if (!toast->has_value())
	{
		ImGui::Text("%s", _S("Press both thumbsticks to display the WiVRn window"));
		return;
	}

	ImGui::Text("%s", (*toast)->content.c_str());
}

void scenes::stream::draw_gui(XrTime predicted_display_time, XrDuration predicted_display_period)
{
	if (auto new_status = next_gui_status.load(); new_status != gui_status)
	{
		spdlog::info("Switch tab from {} to {}", magic_enum::enum_name(gui_status), magic_enum::enum_name(new_status));

		if (not is_gui_interactable() and is_interactable(new_status))
		{
			if (auto head_position = application::locate_controller(application::space(xr::spaces::view), application::space(xr::spaces::world), predicted_display_time))
			{
				world_gui_orientation = head_position->second * head_gui_orientation;
				world_gui_position = head_position->first + glm::mat3_cast(head_position->second) * head_gui_position;
			}
		}
		else if (is_gui_interactable() and not is_interactable(new_status))
		{
			if (auto head_position = application::locate_controller(application::space(xr::spaces::view), application::space(xr::spaces::world), predicted_display_time))
			{
				head_gui_orientation = glm::conjugate(head_position->second) * world_gui_orientation;
				head_gui_position = glm::mat3_cast(glm::conjugate(head_position->second)) * (world_gui_position - head_position->first);
			}
		}

		stored_gui_status = gui_status;
		gui_status = new_status;
		gui_status_last_change = predicted_display_time;

		// Override session state if the GUI is interactable
		if (not is_gui_interactable())
			network_session->send_control(from_headset::session_state_changed{
			        .state = application::get_session_state(),
			});
		else if (application::get_session_state() == XR_SESSION_STATE_FOCUSED)
			network_session->send_control(from_headset::session_state_changed{
			        .state = XR_SESSION_STATE_VISIBLE,
			});

		network_session->send_control(from_headset::stream_tab_changed{.tab = new_status});
	}

	bool interactable = true;
	XrSpace world_space = application::space(xr::spaces::world);
	auto views = session.locate_views(viewconfig, predicted_display_time, world_space).second;

	switch (gui_status)
	{
		case stream_tab::hidden:
		case stream_tab::bitrate_settings:
		case stream_tab::foveation_settings:
		case stream_tab::overlay_only:
		case stream_tab::compact:
			interactable = false;
			break;
		case stream_tab::stats:
		case stream_tab::settings:
		case stream_tab::applications:
		case stream_tab::application_launcher:
			break;
	}
	imgui_ctx->set_controllers_enabled(interactable and not recentering_context);
	if (interactable)
	{
		if (system.hand_tracking_supported())
		{
			if (not left_hand)
				left_hand = session.create_hand_tracker(XR_HAND_LEFT_EXT);
			if (not right_hand)
				right_hand = session.create_hand_tracker(XR_HAND_RIGHT_EXT);
		}
	}
	else
	{
		left_hand.reset();
		right_hand.reset();
	}

	float alpha = 1;
	bool is_urgent = false;
	if (gui_status == stream_tab::hidden)
	{
		auto toast = gui_toast.lock();
		if (toast->has_value())
			is_urgent = (*toast)->is_urgent;

		float t = (predicted_display_time - gui_status_last_change) * 1.e-9f;
		float delay = is_urgent ? constants::stream::urgent_fade_delay : constants::stream::fade_delay;

		alpha = std::clamp<float>(1 - (t - delay) / constants::stream::fade_duration, 0, 1);

		if (alpha == 0)
		{
			toast->reset();
			return;
		}
	}

	// Lock the GUI position to the head, do it before displaying the GUI to avoid being off by one frame when gui_status changes
	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::space(xr::spaces::view), world_space, predicted_display_time);
	if (head_position)
	{
		glm::mat3 M = glm::mat3_cast(head_position->second);
		switch (gui_status)
		{
			case stream_tab::bitrate_settings:
			case stream_tab::foveation_settings:
				imgui_ctx->layers()[0].orientation = head_position->second;
				imgui_ctx->layers()[0].position = head_position->first + M * glm::vec3{0, override_foveation_distance * sin(override_foveation_pitch), -override_foveation_distance};
				break;

			case stream_tab::hidden:
				// Always use the same position for the GUI shortcut tip
				imgui_ctx->layers()[0].orientation = head_position->second;
				imgui_ctx->layers()[0].position = head_position->first + M * glm::vec3{0.0, -0.4, -1.0};
				break;

			case stream_tab::overlay_only:
			case stream_tab::compact:
				imgui_ctx->layers()[0].orientation = head_position->second * head_gui_orientation;
				imgui_ctx->layers()[0].position = head_position->first + M * head_gui_position;
				break;

			case stream_tab::stats:
			case stream_tab::settings:
			case stream_tab::applications:
			case stream_tab::application_launcher:
				imgui_ctx->layers()[0].orientation = world_gui_orientation;
				imgui_ctx->layers()[0].position = world_gui_position;
				break;
		}
	}

	// popup layer floats in front of the main panel so combos and modals pop as their own quad
	imgui_ctx->place_layer_relative(2, 0, constants::gui::popup_position);

	const float tab_width = wivrn::ui::metrics::sidebar_width;
	const float top_bar_h = wivrn::ui::metrics::top_bar_height;
	const float content_margin = wivrn::ui::metrics::content_margin;
	const ImVec2 margin_around_window{50, 50};

	ImGuiStyle & style = ImGui::GetStyle();
	imgui_ctx->new_frame(predicted_display_time);

	// theme the shared cards like the lobby, widget hooks are global so re-point at this scene
	style.FontScaleMain = wivrn::ui::current().font_scale * wivrn::ui::metrics::font_base;
	style.WindowRounding = wivrn::ui::current().card_rounding;
	style.ChildRounding = wivrn::ui::current().card_rounding;
	style.PopupRounding = wivrn::ui::current().card_rounding;
	style.FrameRounding = wivrn::ui::current().rounding;
	style.GrabRounding = wivrn::ui::current().rounding;
	style.TabRounding = wivrn::ui::current().rounding;
	style.Colors[ImGuiCol_Text] = wivrn::ui::current().text;
	style.Colors[ImGuiCol_TextDisabled] = wivrn::ui::current().text_muted;
	wivrn::ui::set_popup_center(imgui_ctx->layers()[2].vp_center(), float(imgui_ctx->layers()[2].vp_size.y));
	wivrn::ui::set_hover_haptic([this] { imgui_ctx->vibrate_on_hover(); });
	wivrn::ui::set_tooltip_hook([this](const char * text) { imgui_ctx->tooltip(text); });

	ImVec2 viewport_size(imgui_ctx->layers()[0].vp_size.x, imgui_ctx->layers()[0].vp_size.y);
	ImVec2 content_size{viewport_size - ImVec2{tab_width, 0} - margin_around_window * 2};
	ImVec2 content_center = margin_around_window + content_size / 2 + ImVec2{tab_width, 0};

	bool display_tabs = false;
	bool always_auto_resize = false;
	switch (gui_status)
	{
		case stream_tab::overlay_only:
			ImGui::SetNextWindowPos(content_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(content_size);
			break;

		case stream_tab::hidden:
		case stream_tab::bitrate_settings:
		case stream_tab::foveation_settings:
			ImGui::SetNextWindowPos(viewport_size / 2, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			always_auto_resize = true;
			break;

		case stream_tab::compact:
			ImGui::SetNextWindowPos(content_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			always_auto_resize = true;
			break;

		case stream_tab::stats:
		case stream_tab::settings:
		case stream_tab::applications:
			ImGui::SetNextWindowPos(margin_around_window);
			ImGui::SetNextWindowSize(viewport_size - margin_around_window * 2);
			display_tabs = true;
			break;
		case stream_tab::application_launcher:
			ImGui::SetNextWindowPos(margin_around_window);
			ImGui::SetNextWindowSize(viewport_size - margin_around_window * 2);
			break;
	}

	if (is_urgent)
	{
		ImGui::PushStyleColor(ImGuiCol_Border, constants::stream::urgent_border_color);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 4);
	}

	// themed translucent background, matching the lobby
	if (display_tabs)
	{
		const wivrn::ui::theme & th = wivrn::ui::current();
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{th.background.x, th.background.y, th.background.z, wivrn::ui::background_alpha()});
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
	if (always_auto_resize)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
		ImGui::Begin("Compact view", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
	}
	else
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		ImGui::Begin("Stream settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	}

	if (is_urgent)
	{
		ImGui::PopStyleColor(1);
		ImGui::PopStyleVar(1);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);

	switch (gui_status)
	{
		case stream_tab::hidden:
			gui_toasts();
			break;

		case stream_tab::overlay_only:
			ImGui::SetCursorPos({20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
			gui_performance_metrics();
			ImGui::EndChild();
			break;

		case stream_tab::compact:
			gui_compact_view();
			break;

		case stream_tab::stats:
			ImGui::SetCursorPos({tab_width + content_margin, top_bar_h});
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - content_margin, 0));
			ImGui::SetCursorPosY(20);
			wivrn::ui::page_header(_S("Statistics"), _S("Live streaming performance."));
			ImGui::BeginChild("plots", {0, 0});
			gui_performance_metrics();
			ImGui::EndChild();
			ImGui::EndChild();
			ImGui::PopStyleVar();
			break;

		case stream_tab::settings:
			ImGui::SetCursorPos({tab_width + content_margin, top_bar_h});
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - content_margin, 0));
			ImGui::SetCursorPosY(20);
			gui_settings(predicted_display_period * 1.e-9f);
			ImGui::Dummy(ImVec2(0, 20));
			ScrollWhenDragging();
			ImGui::EndChild();
			ImGui::PopStyleVar();
			break;

		case stream_tab::bitrate_settings:
			gui_bitrate_settings(predicted_display_period * 1.e-9f);
			break;

		case stream_tab::foveation_settings:
			gui_foveation_settings(predicted_display_period * 1.e-9f);
			break;

		case stream_tab::applications:
			ImGui::SetCursorPos({tab_width + content_margin, top_bar_h});
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - content_margin, 0));
			ImGui::SetCursorPosY(20);
			gui_applications();
			ImGui::Dummy(ImVec2(0, 20));
			ScrollWhenDragging();
			ImGui::EndChild();
			ImGui::PopStyleVar();
			break;

		case stream_tab::application_launcher:
			if (apps.draw_gui(*imgui_ctx, _("Cancel")) != app_launcher::None)
				next_gui_status = stream_tab::applications;
	}

	ImGui::PopStyleVar(2); // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_FrameRounding

	if (display_tabs)
	{
		// top bar: logo left, battery/connection status/window controls right
		const float side = ImGui::GetFrameHeight() * wivrn::ui::metrics::control_height;
		std::vector<wivrn::ui::top_bar_item> top_items;
		if (auto bat = wivrn::gui::battery_status_indicator(instance.now()))
			top_items.push_back({wivrn::ui::chip_width(bat->label, false, side),
			                     [bat = *bat, side] { wivrn::ui::chip(bat.label, bat.style, false, side); }});
		const std::string conn = _("Connected");
		top_items.push_back({wivrn::ui::chip_width(conn, true, side),
		                     [conn, side] { wivrn::ui::chip(conn, wivrn::ui::chip_style::success, true, side); }});
		const std::string close_label = _S("Close");
		top_items.push_back({wivrn::ui::button_width(ICON_FA_XMARK, close_label),
		                     [this, close_label, side] {
			                     if (wivrn::ui::button(ICON_FA_XMARK, close_label, wivrn::ui::button_style::secondary, {0, side}))
				                     next_gui_status = stream_tab::hidden;
		                     }});
		// disconnect asks for confirmation, OpenPopup/confirm_modal share the window id stack
		bool request_disconnect = false;
		const std::string disconnect_label = _S("Disconnect");
		top_items.push_back({wivrn::ui::button_width(ICON_FA_DOOR_OPEN, disconnect_label),
		                     [&request_disconnect, disconnect_label, side] {
			                     if (wivrn::ui::button(ICON_FA_DOOR_OPEN, disconnect_label, wivrn::ui::button_style::danger, {0, side}))
				                     request_disconnect = true;
		                     }});
		wivrn::ui::top_bar(top_bar_h, wivrn_logo, top_items);

		if (request_disconnect)
			ImGui::OpenPopup("confirm disconnect");
		if (wivrn::ui::confirm_modal("confirm disconnect", _("Disconnect"), _("Disconnect from the server and return to the lobby?"), _("Disconnect"), _("Cancel"), true) == 1)
			exit();

		// navigation sidebar, the settings items swap the page but keep the coarse settings tab
		wivrn::ui::begin_sidebar(top_bar_h, tab_width, 2);
		{
			wivrn::ui::nav_section(_S("STREAM"));
			if (wivrn::ui::nav_item(ICON_FA_LIST, _S("Applications"), gui_status == stream_tab::applications))
				next_gui_status = stream_tab::applications;
			if (wivrn::ui::nav_item(ICON_FA_ROCKET, _S("Start"), false))
			{
				apps.reset();
				network_session->send_control(from_headset::get_application_list{
				        .language = application::get_messages_info().language,
				        .country = application::get_messages_info().country,
				        .variant = application::get_messages_info().variant,
				});
				next_gui_status = stream_tab::application_launcher;
			}
			if (wivrn::ui::nav_item(ICON_FA_COMPUTER, _S("Statistics"), gui_status == stream_tab::stats))
				next_gui_status = stream_tab::stats;

			wivrn::ui::nav_section(_S("SETTINGS"));
			auto settings_item = [&](const char * icon, const std::string & label, settings_page page) {
				if (wivrn::ui::nav_item(icon, label, gui_status == stream_tab::settings and current_settings_page == page))
				{
					current_settings_page = page;
					next_gui_status = stream_tab::settings;
				}
			};
			settings_item(ICON_FA_GAUGE_HIGH, _S("Performance"), settings_page::performance);
			settings_item(ICON_FA_TOWER_BROADCAST, _S("Streaming"), settings_page::streaming);
			settings_item(ICON_FA_WAND_MAGIC_SPARKLES, _S("Post-processing"), settings_page::post_processing);
			settings_item(ICON_FA_VOLUME_HIGH, _S("Audio"), settings_page::audio);
			settings_item(ICON_FA_LOCATION_CROSSHAIRS, _S("Tracking"), settings_page::tracking);
			settings_item(ICON_FA_GEARS, _S("System"), settings_page::system);

			// pinned to the bottom
			wivrn::ui::sidebar_footer();
			if (wivrn::ui::nav_item(ICON_FA_CHART_LINE, _S("Statistics overlay"), false))
				next_gui_status = stream_tab::overlay_only;
			if (wivrn::ui::nav_item(ICON_FA_MINIMIZE, _S("Compact view"), false))
				next_gui_status = stream_tab::compact;
		}
		wivrn::ui::end_sidebar();

		wivrn::ui::shell_dividers(top_bar_h, tab_width);
	}
	ImGui::End();
	ImGui::PopStyleVar(2); // ImGuiStyleVar_ChildBorderSize, ImGuiStyleVar_WindowPadding
	if (display_tabs)
		ImGui::PopStyleColor(); // ImGuiCol_WindowBg

	auto layers = imgui_ctx->end_frame();

	// Display controllers and handle recentering
	if (interactable)
	{
		if (recentering_context)
		{
			xr::spaces controller = std::get<0>(*recentering_context);
			bool state;
			switch (controller)
			{
				case xr::spaces::aim_left:
					state = application::read_action_bool(recenter_left).value_or(std::pair{0, false}).second;
					break;
				case xr::spaces::aim_right:
					state = application::read_action_bool(recenter_right).value_or(std::pair{0, false}).second;
					break;
				default:
					state = false;
					break;
			}

			if (state)
				update_gui_position(controller, predicted_display_period * 1e-9f);
			else
				recentering_context.reset();
		}
		else if (auto state = application::read_action_bool(recenter_left); state and state->second)
			update_gui_position(xr::spaces::aim_left, predicted_display_period * 1e-9f);
		else if (auto state = application::read_action_bool(recenter_right); state and state->second)
			update_gui_position(xr::spaces::aim_right, predicted_display_period * 1e-9f);
		else
			recentering_context.reset();

		std::vector<glm::mat4> world_to_window;
		for (auto & window: imgui_ctx->windows())
		{
			if (window.space == xr::spaces::world)
				world_to_window.push_back(glm::inverse(glm::translate(window.position) * glm::mat4(glm::mat3_cast(window.orientation)) * glm::scale(glm::vec3(window.size, 1))));
		}

		bool hide_left_controller = false;
		bool hide_right_controller = false;

		if (left_hand and right_hand)
		{
			auto left = left_hand->locate(world_space, predicted_display_time);
			auto right = right_hand->locate(world_space, predicted_display_time);

			if (left and xr::hand_tracker::check_flags(*left, XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT, 0))
				hide_left_controller = true;

			if (right and xr::hand_tracker::check_flags(*right, XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT, 0))
				hide_right_controller = true;
		}

		input->apply(world,
		             world_space,
		             predicted_display_time,
		             hide_left_controller,
		             hide_left_controller,
		             hide_right_controller,
		             hide_right_controller,
		             world_to_window);

		// Add the layer with the controllers
		if (composition_layer_depth_test_supported)
		{
			render_world(XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
			             world_space,
			             views,
			             width,
			             height,
			             true,
			             layer_controllers,
			             {});
			set_depth_test(true, XR_COMPARE_OP_ALWAYS_FB);
		}
	}

	for (auto [_, layer]: layers)
	{
		add_quad_layer(layer.layerFlags, layer.space, layer.eyeVisibility, layer.subImage, layer.pose, layer.size);
		if (composition_layer_depth_test_supported)
			set_depth_test(true, XR_COMPARE_OP_LESS_FB);

		else if (alpha < 1 and composition_layer_color_scale_bias_supported)
			set_color_scale_bias({alpha, alpha, alpha, alpha}, {});
	}

	// Display the controller rays
	if (interactable)
	{
		render_world(XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		             world_space,
		             views,
		             width,
		             height,
		             composition_layer_depth_test_supported,
		             composition_layer_depth_test_supported ? layer_rays : layer_controllers | layer_rays,
		             {});
		if (composition_layer_depth_test_supported)
			set_depth_test(true, XR_COMPARE_OP_LESS_FB);
	}
}
