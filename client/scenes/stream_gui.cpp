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

#define IMGUI_DEFINE_MATH_OPERATORS

#include "stream.h"

#include "application.h"
#include "constants.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "utils/i18n.h"
#include "utils/ranges.h"
#include <IconsFontAwesome6.h>
#include <chrono>
#include <cmath>
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

void scenes::stream::accumulate_metrics(XrTime predicted_display_time, const std::vector<std::shared_ptr<shard_accumulator::blit_handle>> & blit_handles, const gpu_timestamps & timestamps)
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

// TODO move in separate file, factorize with lobby_gui.cpp
static bool RadioButtonWithoutCheckBox(const std::string & label, bool active, ImVec2 size_arg)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(label.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);

	const ImVec2 pos = window->DC.CursorPos;

	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	ImGuiCol_ col;
	if ((held && hovered) || active)
		col = ImGuiCol_ButtonActive;
	else if (hovered)
		col = ImGuiCol_ButtonHovered;
	else
		col = ImGuiCol_Button;

	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(col), true, style.FrameRounding);

	ImVec2 TextAlign{0, 0.5f};
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label.c_str(), NULL, &label_size, TextAlign, &bb);

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label.c_str(), g.LastItemData.StatusFlags);
	return pressed;
}

template <typename T, typename U>
static bool RadioButtonWithoutCheckBox(const std::string & label, T & v, U v_button, ImVec2 size_arg)
{
	const bool pressed = RadioButtonWithoutCheckBox(label, v == v_button, size_arg);
	if (pressed)
		v = v_button;
	return pressed;
}

void scenes::stream::gui_performance_metrics()
{
	const ImGuiStyle & style = ImGui::GetStyle();

	ImVec2 window_size = ImGui::GetWindowSize() - ImVec2(2, 2) * style.WindowPadding;

	static const std::array plots = {
	        // clang-format off
	        plot(_("CPU time"), {{"",          &global_metric::cpu_time}},     "s"),

	        plot(_("GPU time"), {{_("Reproject"), &global_metric::gpu_time},
		                     {_("Blit"),      &global_metric::gpu_barrier}},  "s"),

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
				        .data = (uintptr_t) & (global_metrics.data()->*data),
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
			        .data = (uintptr_t) & (metrics.data()->encode_begin),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_encode_end{
			        .data = (uintptr_t) & (metrics.data()->encode_end),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_send_begin{
			        .data = (uintptr_t) & (metrics.data()->send_begin),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_send_end{
			        .data = (uintptr_t) & (metrics.data()->send_end),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_first_packet{
			        .data = (uintptr_t) & (metrics.data()->received_first_packet),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_last_packet{
			        .data = (uintptr_t) & (metrics.data()->received_last_packet),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_sent_to_decoder{
			        .data = (uintptr_t) & (metrics.data()->sent_to_decoder),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_received_from_decoder{
			        .data = (uintptr_t) & (metrics.data()->received_from_decoder),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_blitted{
			        .data = (uintptr_t) & (metrics.data()->blitted),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_displayed{
			        .data = (uintptr_t) & (metrics.data()->displayed),
			        .stride = sizeof(decoder_metric),
			        .multiplier = 1e3f};

			getter_data getter_predicted{
			        .data = (uintptr_t) & (metrics.data()->predicted_display),
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
		ImGui::Text(
		        "%s",
		        fmt::format(
		                _F("Estimated motion to photons latency: {}ms"),
		                std::chrono::duration_cast<std::chrono::milliseconds>(
		                        tracking_control.lock()->max_offset)
		                        .count())
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
		  std::chrono::duration_cast<std::chrono::microseconds>(
		          tracking_control.lock()->max_offset)
		                  .count() *
		          1e-3f,
		  "ms");
		ImGui::EndTable();
	}
}

std::string openxr_post_processing_flag_name(XrCompositionLayerSettingsFlagsFB flag); // TODO declaration in a .h file
void scenes::stream::gui_settings()
{
	auto & config = application::get_config();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	if (application::get_openxr_post_processing_supported())
	{
		ImGui::Text("%s", _S("OpenXR post-processing"));
		ImGui::Indent();
		{
			XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.super_sampling;
			if (ImGui::BeginCombo(_S("Supersampling"), openxr_post_processing_flag_name(current).c_str()))
			{
				const XrCompositionLayerSettingsFlagsFB selectable_options[]{
				        0,
				        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB,
				        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB};
				for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
				{
					if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
					{
						spdlog::info("Setting OpenXR super sampling to {}", openxr_post_processing_flag_name(option));
						config.openxr_post_processing.super_sampling = option;
						config.save();
					}
					imgui_ctx->vibrate_on_hover();
				}
				ImGui::EndCombo();
			}
			imgui_ctx->vibrate_on_hover();
			if (ImGui::IsItemHovered())
			{
				imgui_ctx->tooltip(_("Reduce flicker for high contrast edges.\nUseful when the input resolution is high compared to the headset display"));
			}
		}
		{
			XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.sharpening;
			if (ImGui::BeginCombo(_S("Sharpening"), openxr_post_processing_flag_name(current).c_str()))
			{
				const XrCompositionLayerSettingsFlagsFB selectable_options[]{
				        0,
				        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB,
				        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB};
				for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
				{
					if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
					{
						spdlog::info("Setting OpenXR sharpening to {}", openxr_post_processing_flag_name(option));
						config.openxr_post_processing.sharpening = option;
						config.save();
					}
					imgui_ctx->vibrate_on_hover();
				}
				ImGui::EndCombo();
			}
			imgui_ctx->vibrate_on_hover();
			if (ImGui::IsItemHovered())
			{
				imgui_ctx->tooltip(_("Improve clarity of high contrast edges and counteract blur.\nUseful when the input resolution is low compared to the headset display"));
			}
		}
		ImGui::Unindent();

		bool send_packet = false;
		bool save_config = false;
		ImGui::Text("%s", _S("Foveation center override"));
		ImGui::Indent();
		{
			if (ImGui::Checkbox(_S("Enable"), &override_foveation_enable))
			{
				send_packet = true;
				save_config = true;
			}
			imgui_ctx->vibrate_on_hover();

			ImGui::BeginDisabled(!override_foveation_enable);
			ImGui::Text("%s", fmt::format(_F("Height {:.1f} deg"), -override_foveation_pitch * 180 / M_PI).c_str());
			ImGui::Text("%s", fmt::format(_F("Distance {:.2f} m"), override_foveation_distance).c_str());
			if (ImGui::Button(_S("Default")))
			{
				override_foveation_distance = configuration{}.override_foveation_distance;
				override_foveation_pitch = configuration{}.override_foveation_pitch;
				send_packet = true;
				save_config = true;
			}
			imgui_ctx->vibrate_on_hover();

			ImGui::SameLine();

			if (ImGui::Button(_S("Change")))
				gui_status = gui_status::foveation_settings;
			imgui_ctx->vibrate_on_hover();

			ImGui::EndDisabled();
		}
		ImGui::Unindent();

		if (send_packet)
		{
			network_session->send_control(from_headset::override_foveation_center{
			        .enabled = override_foveation_enable,
			        .pitch = override_foveation_pitch,
			        .distance = override_foveation_distance,
			});
		}

		if (save_config)
		{
			auto & config = application::get_config();
			config.override_foveation_enable = override_foveation_enable;
			config.override_foveation_pitch = override_foveation_pitch;
			config.override_foveation_distance = override_foveation_distance;
			config.save();
		}
	}
	ImGui::PopStyleVar();
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
	float delta_pitch = application::read_action_float(foveation_pitch).value_or(std::pair{0, 0}).second * predicted_display_period;

	// Maximum speed 2m/s @ 1m
	float delta_distance = std::exp(std::log(2) * application::read_action_float(foveation_distance).value_or(std::pair{0, 0}).second * predicted_display_period);

	override_foveation_pitch = std::clamp<float>(override_foveation_pitch + delta_pitch, -M_PI / 3, M_PI / 3);
	override_foveation_distance = std::clamp<float>(override_foveation_distance * delta_distance, 0.5, 100);

	bool ok = application::read_action_bool(foveation_ok).value_or(std::pair{0, false}).second;
	bool cancel = application::read_action_bool(foveation_cancel).value_or(std::pair{0, false}).second;

	if (ok)
	{
		gui_status = gui_status::settings;

		// Save settings
		auto & config = application::get_config();
		config.override_foveation_enable = true;
		config.override_foveation_pitch = override_foveation_pitch;
		config.override_foveation_distance = override_foveation_distance;
		config.save();
	}
	else if (cancel)
	{
		gui_status = gui_status::settings;

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

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	CenterTextH(_("Running XR applications:"));
	ImGui::PopFont();
	auto apps = running_applications.lock();
	ImVec2 button_size(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - 20, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 20));
	ImGui::Spacing();
	std::ranges::sort(apps->applications, [](auto & l, auto & r) {
		if (l.overlay == r.overlay)
			return false;
		return r.overlay;
	});
	bool overlay = false;
	for (const auto & app: apps->applications)
	{
		if (app.overlay and not overlay)
		{
			ImGui::Separator();
			CenterTextH(_S("Overlays"));
			overlay = true;
		}
		int colors = 1;
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 20));
		if (app.active or app.overlay)
		{
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32_BLACK_TRANS);
			colors += 2;
		}
		ImGui::SetNextItemAllowOverlap();
		const bool clicked = RadioButtonWithoutCheckBox(
		        std::format("{}{}##{}", app.active ? ICON_FA_CHEVRON_RIGHT " " : "  ", app.name, app.id).c_str(),
		        app.active,
		        button_size);
		if (clicked and not(app.active or app.overlay))
		{
			network_session->send_control(from_headset::set_active_application{.id = app.id});
			imgui_ctx->vibrate_on_hover();
		}
		ImGui::PopStyleColor(colors);
		ImGui::PopStyleVar(2);

		ImGui::SameLine();
		auto right = ImGui::GetWindowSize().x;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
		ImGui::SetCursorPosX(right - ImGui::CalcTextSize(ICON_FA_XMARK).x - ImGui::GetStyle().FramePadding.x - 40);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
		if (ImGui::Button(std::format(ICON_FA_XMARK "##{}", app.id).c_str()))
			network_session->send_control(from_headset::stop_application{.id = app.id});
		imgui_ctx->vibrate_on_hover();
		ImGui::PopStyleColor(3);

		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(_S("Request to quit, may be ignored by the application"));
	}

	auto btn = _("Start");
	ImGui::SetCursorPos(ImGui::GetWindowSize() - ImGui::CalcTextSize(btn.c_str()) - ImVec2(50, 50));
	if (ImGui::Button(btn.c_str()))
		gui_status = gui_status::application_launcher;
	imgui_ctx->vibrate_on_hover();
	ImGui::PopStyleVar(3);
}

// Return the vector v such that dot(v, x) > 0 iff x is on the side where the composition layer is visible
static glm::vec4 compute_ray_limits(const XrPosef & pose, float margin = 0)
{
	glm::quat q{
	        pose.orientation.w,
	        pose.orientation.x,
	        pose.orientation.y,
	        pose.orientation.z,
	};

	glm::vec3 p{
	        pose.position.x,
	        pose.position.y,
	        pose.position.z,
	};

	glm::vec3 normal = glm::column(glm::mat3_cast(q), 2);

	return glm::vec4(normal, -glm::dot(p, normal) - margin);
}

void scenes::stream::draw_gui(XrTime predicted_display_time, XrDuration predicted_display_period)
{
	if (not(plots_toggle_1 and plots_toggle_2))
		return;
	bool interactable = true;
	XrSpace world_space = application::space(xr::spaces::world);
	auto views = session.locate_views(viewconfig, predicted_display_time, world_space).second;

	switch (gui_status)
	{
		case gui_status::hidden:
		case gui_status::foveation_settings:
		case gui_status::overlay_only:
		case gui_status::compact:
			interactable = false;
			break;
		case gui_status::stats:
		case gui_status::settings:
		case gui_status::applications:
		case gui_status::application_launcher:
			break;
	}
	imgui_ctx->set_controllers_enabled(interactable);
	if (interactable)
	{
		if (system.hand_tracking_supported())
		{
			left_hand = session.create_hand_tracker(XR_HAND_LEFT_EXT);
			right_hand = session.create_hand_tracker(XR_HAND_RIGHT_EXT);
		}
	}
	else
	{
		left_hand.reset();
		right_hand.reset();
	}

	if (gui_status != last_gui_status)
	{
		last_gui_status = gui_status;
		if (is_gui_interactable())
			next_gui_status = gui_status;
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
	}

	float alpha = 1;
	if (gui_status == gui_status::hidden)
	{
		float t = (predicted_display_time - gui_status_last_change) * 1.e-9f;

		alpha = std::clamp<float>(1 - (t - constants::stream::fade_delay) / constants::stream::fade_duration, 0, 1);

		if (alpha == 0)
			return;
	}

	// Lock the GUI position to the head, do it before displaying the GUI to avoid being off by one frame when gui_status changes
	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::space(xr::spaces::view), world_space, predicted_display_time);
	if (head_position)
	{
		glm::mat3 M = glm::mat3_cast(head_position->second);
		switch (gui_status)
		{
			case gui_status::foveation_settings:
				imgui_ctx->layers()[0].orientation = head_position->second;
				imgui_ctx->layers()[0].position = head_position->first + M * glm::vec3{0, -override_foveation_distance * sin(override_foveation_pitch), -override_foveation_distance};
				break;

			case gui_status::hidden:
				// Always use the same position for the GUI shortcut tip
				imgui_ctx->layers()[0].orientation = head_position->second;
				imgui_ctx->layers()[0].position = head_position->first + M * glm::vec3{0.0, -0.4, -1.0};
				break;

			case gui_status::overlay_only:
			case gui_status::compact:
			case gui_status::stats:
			case gui_status::settings:
			case gui_status::applications:
			case gui_status::application_launcher:
				imgui_ctx->layers()[0].orientation = head_position->second * head_gui_orientation;
				imgui_ctx->layers()[0].position = head_position->first + M * head_gui_position;
				break;
		}
	}

	const float tab_width = 300;
	const ImVec2 margin_around_window{50, 50};

	const ImGuiStyle & style = ImGui::GetStyle();
	imgui_ctx->new_frame(predicted_display_time);

	ImVec2 content_size{ImGui::GetMainViewport()->Size - ImVec2{tab_width, 0} - margin_around_window * 2};
	ImVec2 content_center = margin_around_window + content_size / 2 + ImVec2{tab_width, 0};

	bool display_tabs, always_auto_resize;
	switch (gui_status)
	{
		case gui_status::overlay_only:
			ImGui::SetNextWindowPos(content_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(content_size);
			always_auto_resize = false;
			display_tabs = false;
			break;

		case gui_status::hidden:
		case gui_status::foveation_settings:
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Size / 2, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			always_auto_resize = true;
			display_tabs = false;
			break;

		case gui_status::compact:
			ImGui::SetNextWindowPos(content_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			always_auto_resize = true;
			display_tabs = false;
			break;

		case gui_status::stats:
		case gui_status::settings:
		case gui_status::applications:
			ImGui::SetNextWindowPos(margin_around_window);
			ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - margin_around_window * 2);
			always_auto_resize = false;
			display_tabs = true;
			break;
		case gui_status::application_launcher:
			ImGui::SetNextWindowPos(margin_around_window);
			ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - margin_around_window * 2);
			always_auto_resize = false;
			display_tabs = false;
			break;
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

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);

	switch (gui_status)
	{
		case gui_status::hidden:
			ImGui::Text("%s", _S("Press both thumbsticks to display the WiVRn window"));
			break;

		case gui_status::overlay_only:
			ImGui::SetCursorPos({20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
			gui_performance_metrics();
			ImGui::EndChild();
			break;

		case gui_status::compact:
			gui_compact_view();
			break;

		case gui_status::stats:
			ImGui::SetCursorPos({tab_width + 20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
			gui_performance_metrics();
			ImGui::EndChild();
			break;

		case gui_status::settings:
			ImGui::SetCursorPos({tab_width + 20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
			gui_settings();
			ImGui::EndChild();
			break;

		case gui_status::foveation_settings:
			gui_foveation_settings(predicted_display_period * 1.e-9f);
			break;

		case gui_status::applications:
			ImGui::SetCursorPos({tab_width + 20, 20});
			ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
			gui_applications();
			ImGui::EndChild();
			break;

		case gui_status::application_launcher:
			if (apps.draw_gui(*imgui_ctx, _("Cancel")) != app_launcher::None)
				gui_status = gui_status::applications;
	}

	ImGui::PopStyleVar(2); // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_FrameRounding

	if (display_tabs)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorPos(style.WindowPadding);
		{
			ImGui::BeginChild("Tabs", {tab_width, ImGui::GetContentRegionMax().y - ImGui::GetWindowContentRegionMin().y});

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
			RadioButtonWithoutCheckBox(ICON_FA_COMPUTER "  " + _("Stats"), gui_status, gui_status::stats, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_GEARS "  " + _("Settings"), gui_status, gui_status::settings, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_LIST "  " + _("Applications"), gui_status, gui_status::applications, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			int n_items_at_end = 4;
			ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - n_items_at_end * ImGui::GetCurrentContext()->FontSize - (n_items_at_end * 2) * style.FramePadding.y - (n_items_at_end - 1) * style.ItemSpacing.y - style.WindowPadding.y);

			RadioButtonWithoutCheckBox(ICON_FA_CHART_LINE "  " + _("Statistics overlay"), gui_status, gui_status::overlay_only, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_MINIMIZE "  " + _("Compact view"), gui_status, gui_status::compact, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_XMARK "  " + _("Close"), gui_status, gui_status::hidden, {tab_width, 0});
			imgui_ctx->vibrate_on_hover();

			bool dummy = false;
			if (RadioButtonWithoutCheckBox(ICON_FA_DOOR_OPEN "  " + _("Disconnect"), dummy, true, {tab_width, 0}))
				exit();
			imgui_ctx->vibrate_on_hover();

			ImGui::PopStyleVar(); // ImGuiStyleVar_FramePadding
			ImGui::EndChild();
		}
		ImGui::PopStyleColor(); // ImGuiCol_ChildBg
	}
	ImGui::End();
	ImGui::PopStyleVar(2); // ImGuiStyleVar_ChildBorderSize, ImGuiStyleVar_WindowPadding

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
				update_gui_position(controller);
			else
				recentering_context.reset();
		}
		else if (auto state = application::read_action_bool(recenter_left); state and state->second)
			update_gui_position(xr::spaces::aim_left);
		else if (auto state = application::read_action_bool(recenter_right); state and state->second)
			update_gui_position(xr::spaces::aim_right);
		else
			recentering_context.reset();

		std::vector<glm::vec4> ray_limits;

		for (auto [_, layer]: layers)
			ray_limits.push_back(compute_ray_limits(layer.pose));

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

		input->apply(world, world_space, predicted_display_time, hide_left_controller, hide_right_controller, ray_limits);

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

	// Add the layer with the GUI
	for (auto [_, layer]: layers)
	{
		add_quad_layer(layer.layerFlags, layer.space, layer.eyeVisibility, layer.subImage, layer.pose, layer.size);
		if (composition_layer_depth_test_supported)
			set_depth_test(true, XR_COMPARE_OP_LESS_FB);
		if (alpha < 1 and composition_layer_color_scale_bias_supported)
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
