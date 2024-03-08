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
#include "implot.h"
#include "utils/ranges.h"
#include <cmath>
#include <spdlog/spdlog.h>
#include <limits>

namespace
{
float compute_plot_max_value(float * data, int count, ptrdiff_t stride)
{
	float max = 0;
	uintptr_t ptr = (uintptr_t)data;
	for(int i = 0; i < count; i++)
	{
		max = std::max(max, *(float*)(ptr + i * stride));
	}

	// First power of 10 less than the max
	float x = pow(10, floor(log10(max)));
	return ceil(max / x) * x;
}

std::pair<float, std::string> compute_plot_unit(float max_value)
{
	if (max_value > 1e9)
		return { 1e-9, "G" };
	if (max_value > 1e6)
		return { 1e-6, "M" };
	if (max_value > 1e3)
		return { 1e-3, "k" };
	if (max_value > 1)
		return { 1, "" };
	if (max_value > 1e-3)
		return { 1e3, "m" };
	if (max_value > 1e-6)
		return { 1e6, "u" };
	return { 1e9, "n" };
}

struct getter_data
{
	uintptr_t data;
	int stride;
	int offset;
	int count;
	float multiplier;
};

ImPlotPoint getter(int index, void * data_)
{
	getter_data& data = *(getter_data*)data_;

	int offset_index = (index + data.offset) % data.count;

	return ImPlotPoint(index, *(float*)(data.data + offset_index * data.stride) * data.multiplier);
}
}

void scenes::stream::accumulate_metrics(XrTime predicted_display_time, const std::vector<std::shared_ptr<shard_accumulator::blit_handle>>& blit_handles, const gpu_timestamps& timestamps)
{
	uint64_t rx = network_session->bytes_received();
	uint64_t tx = network_session->bytes_sent();

	float dt = (predicted_display_time - last_metric_time) * 1e-9f;

	bandwidth_rx = 0.8 * bandwidth_rx + 0.2 * float(rx - bytes_received) / dt;
	bandwidth_tx = 0.8 * bandwidth_tx + 0.2 * float(tx - bytes_sent ) /dt;

	last_metric_time = predicted_display_time;
	bytes_received = rx;
	bytes_sent = tx;

	*(gpu_timestamps*)&global_metrics[metrics_offset] = timestamps;
	global_metrics[metrics_offset].cpu_time = application::get_cpu_time().count() * 1e-9f;
	global_metrics[metrics_offset].bandwidth_rx = bandwidth_rx * 8;
	global_metrics[metrics_offset].bandwidth_tx = bandwidth_tx * 8;

	if (decoder_metrics.size() != blit_handles.size())
		decoder_metrics.resize(blit_handles.size());

	uint64_t min_encode_begin = std::numeric_limits<uint64_t>::max();
	for(const auto& bh: blit_handles)
		min_encode_begin = std::min(min_encode_begin, bh->timing_info.encode_begin);

	for(auto&& [metrics, bh]: utils::zip(decoder_metrics, blit_handles))
	{
		if (metrics.size() != global_metrics.size())
			metrics.resize(global_metrics.size());

		metrics[metrics_offset] = decoder_metric{
			.send_begin            = (bh->timing_info.send_begin         - min_encode_begin) * 1e-9f,
			.send_end              = (bh->timing_info.send_end           - min_encode_begin) * 1e-9f,
			.received_first_packet = (bh->feedback.received_first_packet - min_encode_begin) * 1e-9f,
			.received_last_packet  = (bh->feedback.received_last_packet  - min_encode_begin) * 1e-9f,
			.sent_to_decoder       = (bh->feedback.sent_to_decoder       - min_encode_begin) * 1e-9f,
			.received_from_decoder = (bh->feedback.received_from_decoder - min_encode_begin) * 1e-9f,
			.blitted               = (bh->feedback.blitted               - min_encode_begin) * 1e-9f,
			.displayed             = (bh->feedback.displayed             - min_encode_begin) * 1e-9f,
		};
	}

	metrics_offset = (metrics_offset + 1) % global_metrics.size();
}

XrCompositionLayerQuad scenes::stream::plot_performance_metrics(XrTime predicted_display_time)
{
	imgui_ctx->new_frame(predicted_display_time);
	const ImGuiStyle & style = ImGui::GetStyle();

	ImGui::SetNextWindowPos({0, 0});
	ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
	ImGui::Begin("Performance metrics", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImVec2 window_size = ImGui::GetWindowSize() - ImVec2(2,2) * style.WindowPadding;

	static const std::array plots = {
	        plot("CPU time", {
	                                 {"", &global_metric::cpu_time},
	                         },
	             "s"),
	        plot("GPU time", {
	                                 {"Reproject", &global_metric::gpu_time},
	                                 {"Blit", &global_metric::gpu_barrier},
	                         },
	             "s"),
	        plot("Network", {
	                                {"Download", &global_metric::bandwidth_rx},
	                                {"Upload", &global_metric::bandwidth_tx},
	                        },
	             "bit/s"),
	};

	int n_plots = plots.size() + decoders.size();
	axis_scale.resize(n_plots);

	int n_cols = 2;
	int n_rows = ceil((float)n_plots / n_cols);

	ImVec2 plot_size = ImVec2(
		window_size.x / n_cols - style.ItemSpacing.x * (n_cols-1) / n_cols,
		window_size.y / n_rows - style.ItemSpacing.y * (n_rows-1) / n_rows);

	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(32, 32, 32, 64));
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, IM_COL32(0, 0, 0, 0));

	int n = 0;
	for(const auto& [title, subplots, unit]: plots)
	{
		if (ImPlot::BeginPlot(title, plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoChild))
		{
			float min_v = 0;
			float max_v = 0;
			for (const auto& [subtitle, data]: subplots)
			{
				max_v = std::max(max_v, compute_plot_max_value(&(global_metrics.data()->*data), global_metrics.size(), sizeof(global_metric)));
			}
			auto [ multiplier, prefix ] = compute_plot_unit(max_v);

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

			for (const auto& [subtitle, data]: subplots)
			{
				getter_data gdata{
				        .data = (uintptr_t) & (global_metrics.data()->*data),
				        .stride = sizeof(global_metric),
				        .offset = metrics_offset,
				        .count = (int)global_metrics.size(),
				        .multiplier = multiplier,
				};
				ImPlot::PlotLineG(subtitle, getter, &gdata, global_metrics.size(), ImPlotLineFlags_Shaded);
			}
			ImPlot::EndPlot();
		}

		if (++n % n_cols != 0)
			ImGui::SameLine();
	}

	for(auto&& [index, metrics]: utils::enumerate(decoder_metrics))
	{
		std::string title = "Decoder " + std::to_string(index);
		if (ImPlot::BeginPlot(title.c_str(), plot_size, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoChild))
		{
			float min_v = 0;
			float max_v = compute_plot_max_value(&(metrics.data()->displayed), metrics.size(), sizeof(decoder_metric));
			// auto [ multiplier, prefix ] = compute_plot_unit(max_v);

			if (axis_scale[n] == 0)
				axis_scale[n] = max_v;
			else
				axis_scale[n] = 0.99 * axis_scale[n] + 0.01 * max_v;

			const char * title_with_units = "Timings [ms]";
			ImPlot::SetupAxes(nullptr, title_with_units, ImPlotAxisFlags_NoDecorations, 0);
			ImPlot::SetupAxesLimits(0, metrics.size() - 1, min_v * 1e3f, axis_scale[n] * 1e3f, ImGuiCond_Always);


			getter_data getter_send_begin{
				.data = (uintptr_t)&(metrics.data()->send_begin),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_send_end{
				.data = (uintptr_t)&(metrics.data()->send_end),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_received_first_packet{
				.data = (uintptr_t)&(metrics.data()->received_first_packet),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_received_last_packet{
				.data = (uintptr_t)&(metrics.data()->received_last_packet),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_sent_to_decoder{
				.data = (uintptr_t)&(metrics.data()->sent_to_decoder),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_received_from_decoder{
				.data = (uintptr_t)&(metrics.data()->received_from_decoder),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_blitted{
				.data = (uintptr_t)&(metrics.data()->blitted),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			getter_data getter_displayed{
				.data = (uintptr_t)&(metrics.data()->displayed),
				.stride = sizeof(decoder_metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = 1e3f
			};

			ImPlot::PlotShadedG("Send",    getter, &getter_send_begin,            getter, &getter_send_end,              metrics.size());
			ImPlot::PlotShadedG("Receive", getter, &getter_received_first_packet, getter, &getter_received_last_packet,  metrics.size());
			ImPlot::PlotShadedG("Decode",  getter, &getter_sent_to_decoder,       getter, &getter_received_from_decoder, metrics.size());
			ImPlot::PlotLineG("Blitted",   getter, &getter_blitted,                                                      metrics.size());
			ImPlot::PlotLineG("Displayed", getter, &getter_displayed,                                                    metrics.size());

			ImPlot::EndPlot();
		}

		if (++n % n_cols != 0)
			ImGui::SameLine();

	}

	ImPlot::PopStyleColor(5);
	ImGui::End();

	return imgui_ctx->end_frame();
}
