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
#include "imgui_internal.h"
#include "implot.h"
#include "utils/i18n.h"
#include "utils/ranges.h"
#include <IconsFontAwesome6.h>
#include <cmath>
#include <limits>
#include <ranges>
#include <spdlog/spdlog.h>

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

	bandwidth_rx = 0.8 * bandwidth_rx + 0.2 * float(rx - bytes_received) / dt;
	bandwidth_tx = 0.8 * bandwidth_tx + 0.2 * float(tx - bytes_sent) / dt;

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

template <typename T>
static bool RadioButtonWithoutCheckBox(const std::string & label, T & v, T v_button, ImVec2 size_arg)
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
		if (ImPlot::BeginPlot(title.c_str(), plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoChild))
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
		if (ImPlot::BeginPlot(title.c_str(), plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoChild))
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
		std::lock_guard lock(tracking_control_mutex);
		ImGui::Text("%s", fmt::format(_F("Estimated motion to photons latency: {}ms"), std::chrono::duration_cast<std::chrono::milliseconds>(tracking_control.max_offset).count()).c_str());

		if (gui_status == gui_status::interactable)
			ImGui::Text("%s", _S("Press both thumbsticks to return to the game, press the grip button to move the window"));
		else
			ImGui::Text("%s", _S("Press both thumbsticks to interact"));
	}
}

std::string openxr_post_processing_flag_name(XrCompositionLayerSettingsFlagsFB flag);
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
				// tooltip(_("Reduce flicker for high contrast edges.\nUseful when the input resolution is high compared to the headset display"));
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
				// tooltip(_("Improve clarity of high contrast edges and counteract blur.\nUseful when the input resolution is low compared to the headset display"));
			}
		}
		ImGui::Unindent();
	}
	ImGui::PopStyleVar();
}

std::vector<XrCompositionLayerQuad> scenes::stream::draw_gui(XrTime predicted_display_time)
{
	const float TabWidth = 300;

	const ImGuiStyle & style = ImGui::GetStyle();
	imgui_ctx->new_frame(predicted_display_time);

	bool interactable = gui_status == gui_status::interactable;

	if (interactable)
	{
		ImGui::SetNextWindowPos({50, 50});
		ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - ImVec2{100, 100});
	}
	else
	{
		ImGui::SetNextWindowPos({TabWidth + 50, 50});
		ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - ImVec2{TabWidth + 100, 100});
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	ImGui::Begin("Performance metrics", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	if (interactable)
		ImGui::SetCursorPos({TabWidth + 20, 20});
	else
		ImGui::SetCursorPos({20, 20});

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
	ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
	switch (current_tab)
	{
		case tab::stats:
			gui_performance_metrics();
			break;

		case tab::settings:
			gui_settings();
			break;

		case tab::disconnect:
			// TODO disconnect correctly
			// application::pop_scene();
			break;

		case tab::hide:
			break;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	if (interactable)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorPos(style.WindowPadding);
		{
			ImGui::BeginChild("Tabs", {TabWidth, ImGui::GetContentRegionMax().y - ImGui::GetWindowContentRegionMin().y});

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
			RadioButtonWithoutCheckBox(ICON_FA_COMPUTER "  " + _("Stats"), current_tab, tab::stats, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_GEARS "  " + _("Settings"), current_tab, tab::settings, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			int n_items_at_end = 2;
			ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - n_items_at_end * ImGui::GetCurrentContext()->FontSize - (n_items_at_end * 2) * style.FramePadding.y - (n_items_at_end - 1) * style.ItemSpacing.y - style.WindowPadding.y);

			RadioButtonWithoutCheckBox(ICON_FA_EYE_SLASH "  " + _("Hide"), current_tab, tab::hide, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_DOOR_OPEN "  " + _("Disconnect"), current_tab, tab::disconnect, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			ImGui::PopStyleVar(); // ImGuiStyleVar_FramePadding
			ImGui::EndChild();
		}
		ImGui::PopStyleColor(); // ImGuiCol_ChildBg
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	if (current_tab == tab::hide)
	{
		gui_status = gui_status::hidden;
		current_tab = tab::stats;
	}

	std::vector<XrCompositionLayerQuad> layers;
	for (auto & layer: imgui_ctx->end_frame())
		layers.push_back(layer.second);

	return layers;
}
