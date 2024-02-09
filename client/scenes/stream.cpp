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

#include <vulkan/vulkan_raii.hpp>
#define GLM_FORCE_RADIANS

#include "stream.h"

#include "application.h"
#include "decoder/shard_accumulator.h"
#include "spdlog/spdlog.h"
#include "utils/ranges.h"
#include "utils/sync_queue.h"
#include "utils/named_thread.h"
#include "wivrn_packets.h"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "audio/audio.h"
#include "hardware.h"
#include "implot.h"

using namespace xrt::drivers::wivrn;

// clang-format off
static const std::unordered_map<std::string, device_id> device_ids = {
	{"/user/hand/left/input/x/click",           device_id::X_CLICK},
	{"/user/hand/left/input/x/touch",           device_id::X_TOUCH},
	{"/user/hand/left/input/y/click",           device_id::Y_CLICK},
	{"/user/hand/left/input/y/touch",           device_id::Y_TOUCH},
	{"/user/hand/left/input/menu/click",        device_id::MENU_CLICK},
	{"/user/hand/left/input/squeeze/value",     device_id::LEFT_SQUEEZE_VALUE},
	{"/user/hand/left/input/trigger/value",     device_id::LEFT_TRIGGER_VALUE},
	{"/user/hand/left/input/trigger/touch",     device_id::LEFT_TRIGGER_TOUCH},
	{"/user/hand/left/input/thumbstick",        device_id::LEFT_THUMBSTICK_X},
	{"/user/hand/left/input/thumbstick/click",  device_id::LEFT_THUMBSTICK_CLICK},
	{"/user/hand/left/input/thumbstick/touch",  device_id::LEFT_THUMBSTICK_TOUCH},
	{"/user/hand/left/input/thumbrest/touch",   device_id::LEFT_THUMBREST_TOUCH},
	{"/user/hand/right/input/a/click",          device_id::A_CLICK},
	{"/user/hand/right/input/a/touch",          device_id::A_TOUCH},
	{"/user/hand/right/input/b/click",          device_id::B_CLICK},
	{"/user/hand/right/input/b/touch",          device_id::B_TOUCH},
	{"/user/hand/right/input/system/click",     device_id::SYSTEM_CLICK},
	{"/user/hand/right/input/squeeze/value",    device_id::RIGHT_SQUEEZE_VALUE},
	{"/user/hand/right/input/trigger/value",    device_id::RIGHT_TRIGGER_VALUE},
	{"/user/hand/right/input/trigger/touch",    device_id::RIGHT_TRIGGER_TOUCH},
	{"/user/hand/right/input/thumbstick",       device_id::RIGHT_THUMBSTICK_X},
	{"/user/hand/right/input/thumbstick/click", device_id::RIGHT_THUMBSTICK_CLICK},
	{"/user/hand/right/input/thumbstick/touch", device_id::RIGHT_THUMBSTICK_TOUCH},
	{"/user/hand/right/input/thumbrest/touch",  device_id::RIGHT_THUMBREST_TOUCH},
};
// clang-format on

static const std::array supported_formats =
{
	vk::Format::eR8G8B8A8Srgb,
	vk::Format::eB8G8R8A8Srgb
};

std::shared_ptr<scenes::stream> scenes::stream::create(std::unique_ptr<wivrn_session> network_session, bool show_performance_metrics)
{
	std::shared_ptr<stream> self{new stream};
	spdlog::info("decoder_mutex.native_handle() = {}", (void*)self->decoder_mutex.native_handle());

	self->network_session = std::move(network_session);
	self->show_performance_metrics = show_performance_metrics;

	from_headset::headset_info_packet info{};

	auto view = self->system.view_configuration_views(self->viewconfig)[0];
	view = override_view(view, guess_model());

	info.recommended_eye_width = view.recommendedImageRectWidth;
	info.recommended_eye_height = view.recommendedImageRectHeight;

	auto [flags, views] = self->session.locate_views(
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	        self->instance.now(),
	        application::view());
	assert(views.size() == info.fov.size());
	for (auto [i, j]: utils::zip(views, info.fov))
	{
		j = i.fov;
	}

	if (self->instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		info.available_refresh_rates = self->session.get_refresh_rates();
		info.preferred_refresh_rate = self->session.get_current_refresh_rate();
	}

	if (info.available_refresh_rates.empty())
		spdlog::warn("Unable to detect refresh rates");

	audio::get_audio_description(info);

	self->network_session->send_control(info);

	self->network_thread = utils::named_thread("network_thread", &stream::process_packets, self.get());

	self->video_thread = utils::named_thread("video_thread", &stream::video, self.get());

	self->command_buffer = std::move(self->device.allocateCommandBuffers({
		.commandPool = *self->commandpool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1})[0]);
	self->fence = self->device.createFence({});

	// Look up the XrActions for haptics
	self->haptics_actions[0].first = application::get_action("/user/hand/left/output/haptic").first;
	self->haptics_actions[0].second = application::string_to_path("/user/hand/left");

	self->haptics_actions[1].first = application::get_action("/user/hand/right/output/haptic").first;
	self->haptics_actions[1].second = application::string_to_path("/user/hand/right");

	// Look up the XrActions for input
	for (const auto & [action, action_type, name]: application::inputs())
	{
		auto it = device_ids.find(name);

		if (it == device_ids.end())
			continue;

		self->input_actions.emplace_back(it->second, action, action_type);
	}

	self->swapchain_format = vk::Format::eUndefined;
	spdlog::info("Supported swapchain formats:");

	for (auto format: self->session.get_swapchain_formats())
	{
		spdlog::info("    {}", vk::to_string(format));
	}
	for (auto format: self->session.get_swapchain_formats())
	{
		if (std::find(supported_formats.begin(), supported_formats.end(), format) != supported_formats.end())
		{
			self->swapchain_format = format;
			break;
		}
	}

	if (self->swapchain_format == vk::Format::eUndefined)
		throw std::runtime_error("No supported swapchain format");

	spdlog::info("Using format {}", vk::to_string(self->swapchain_format));

	return self;
}

void scenes::stream::on_focused()
{
	if (show_performance_metrics)
	{
		swapchain_imgui = xr::swapchain(
			session,
			device,
			swapchain_format,
			1500, 1000);

		imgui_ctx.emplace(physical_device,
				device,
				queue_family_index,
				queue,
				view_space,
				std::span<imgui_context::controller>{},
				swapchain_imgui,
				glm::vec2{1.0, 0.6666});

		imgui_ctx->set_position({0,0,-1}, {1,0,0,0});
	}
}

void scenes::stream::on_unfocused()
{
	imgui_ctx.reset();
	swapchain_imgui.reset();
}

scenes::stream::~stream()
{
	exit();

	if (video_thread.joinable())
		video_thread.join();

	if (tracking_thread && tracking_thread->joinable())
		tracking_thread->join();

	if (network_thread.joinable())
		network_thread.join();
}

void scenes::stream::push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle)
{
	std::shared_ptr<shard_accumulator::blit_handle> removed;

	if (!application::is_visible())
		return;

	{
		std::unique_lock lock(decoder_mutex);
		for (auto & i: decoders)
		{
			if (i.decoder.get() == decoder)
			{
				static_assert(std::tuple_size_v<decltype(i.latest_frames)> == 2);
				std::swap(removed, i.latest_frames[0]);
				std::swap(i.latest_frames[0], i.latest_frames[1]);
				std::swap(i.latest_frames[1], handle);
				break;
			}
		}

		if (!ready_ && std::all_of(decoders.begin(), decoders.end(), [](accumulator_images & i) {
			    return i.latest_frames.back();
		    }))
		{
			ready_ = true;
			first_frame_time = application::now();
			spdlog::info("Stream scene ready at t={}", first_frame_time);
		}
	}

	if (removed and not removed->feedback.blitted)
	{
		send_feedback(removed->feedback);
	}
}

std::vector<uint64_t> scenes::stream::accumulator_images::frames() const
{
	std::vector<uint64_t> result;
	for (const auto & frame: latest_frames)
	{
		if (frame)
			result.push_back(frame->feedback.frame_index);
	}
	return result;
}

std::optional<uint64_t> scenes::stream::accumulator_images::common_frame(const std::vector<accumulator_images> & sets)
{
	if (sets.empty())
		return {};
	auto common_frames = sets[0].frames();
	for (const auto & set: sets)
	{
		std::vector<uint64_t> tmp;
		auto x = set.frames();
		std::set_intersection(
		        x.begin(), x.end(), common_frames.begin(), common_frames.end(), std::back_inserter(tmp));
		common_frames = tmp;
		if (common_frames.empty())
			return {};
	}
	assert(not common_frames.empty());
	return common_frames.back();
}

std::shared_ptr<shard_accumulator::blit_handle> scenes::stream::accumulator_images::frame(std::optional<uint64_t> id)
{
	for (auto it = latest_frames.rbegin(); it != latest_frames.rend(); ++it)
	{
		if (not *it)
			continue;
		if (id and (*it)->feedback.frame_index != *id)
			continue;
		return *it;
	}
	return nullptr;
}

scenes::stream::metric scenes::stream::get_metrics(XrTime predicted_display_time)
{
	uint64_t rx = network_session->bytes_received();
	uint64_t tx = network_session->bytes_sent();

	float dt = (predicted_display_time - last_metric_time) * 1e-9f;

	bandwidth_rx = 0.8 * bandwidth_rx + 0.2 * float(rx - bytes_received) / dt;
	bandwidth_tx = 0.8 * bandwidth_tx + 0.2 * float(tx - bytes_sent ) /dt;

	last_metric_time = predicted_display_time;
	bytes_received = rx;
	bytes_sent = tx;

	return metric{
		.cpu_time = application::get_cpu_time().count() * 1e-9f,
		.gpu_time = application::get_gpu_time().count() * 1e-9f,
		.bandwidth_rx = bandwidth_rx * 8,
		.bandwidth_tx = bandwidth_tx * 8,
	};
}

static float compute_plot_max_value(float * data, int count, ptrdiff_t stride)
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

static std::pair<float, std::string> compute_plot_unit(float max_value)
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

static ImPlotPoint getter(int index, void * data_)
{
	getter_data& data = *(getter_data*)data_;

	int offset_index = (index + data.offset) % data.count;

	return ImPlotPoint(index, *(float*)(data.data + offset_index * data.stride) * data.multiplier);
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

	metrics[metrics_offset] = get_metrics(predicted_display_time);
	metrics_offset = (metrics_offset + 1) % metrics.size();

	static const std::array plots = {
		std::make_tuple(ImPlot::GetColormapColor(0), "CPU time", "s",   &metric::cpu_time),
		std::make_tuple(ImPlot::GetColormapColor(1), "GPU time", "s",   &metric::gpu_time),
		std::make_tuple(ImPlot::GetColormapColor(2), "Download", "bit/s", &metric::bandwidth_rx),
		std::make_tuple(ImPlot::GetColormapColor(3), "Upload",   "bit/s", &metric::bandwidth_tx),
	};

	axis_scale.resize(plots.size());

	int n_cols = 2;
	int n_rows = ceil((float)plots.size() / n_cols);

	ImVec2 plot_size = ImVec2(
		window_size.x / n_cols - style.ItemSpacing.x * (n_cols-1) / n_cols,
		window_size.y / n_rows - style.ItemSpacing.y * (n_rows-1) / n_rows);

	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(32, 32, 32, 64));
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, IM_COL32(0, 0, 0, 0));

	int n = 0;
	for(auto [color, title, unit, data]: plots)
	{
		if (ImPlot::BeginPlot(title, plot_size, ImPlotFlags_CanvasOnly|ImPlotFlags_NoChild))
		{
			float min_v = 0;
			float max_v = compute_plot_max_value(&(metrics.data()->*data), metrics.size(), sizeof(metric));
			auto [ multiplier, prefix ] = compute_plot_unit(max_v);

			if (axis_scale[n] == 0)
				axis_scale[n] = max_v;
			else
				axis_scale[n] = 0.99 * axis_scale[n] + 0.01 * max_v;

			getter_data gdata{
				.data = (uintptr_t)&(metrics.data()->*data),
				.stride = sizeof(metric),
				.offset = metrics_offset,
				.count = (int)metrics.size(),
				.multiplier = multiplier
			};

			std::string title_with_units = std::string(title) + " [" + prefix + unit + "]";
			ImPlot::SetupAxes(nullptr, title_with_units.c_str(), ImPlotAxisFlags_NoDecorations, 0);
			ImPlot::SetupAxesLimits(0, metrics.size() - 1, min_v * multiplier, axis_scale[n] * multiplier, ImGuiCond_Always);
			ImPlot::SetNextLineStyle(color);
			ImPlot::SetNextFillStyle(color, 0.25);
			ImPlot::PlotLineG(title, getter, &gdata, metrics.size(), ImPlotLineFlags_Shaded);
			// ImPlot::PlotLine(title, &(metrics.data()->*data), metrics.size(), 1, 0, ImPlotLineFlags_Shaded, metrics_offset, sizeof(metric));
			ImPlot::EndPlot();
		}

		if (++n % 2 != 0)
			ImGui::SameLine();
	}

	ImPlot::PopStyleColor(5);
	ImGui::End();

	return imgui_ctx->end_frame();
}

void scenes::stream::render(XrTime predicted_display_time, bool should_render)
{
	if (exiting)
		application::pop_scene();

	if (decoders.empty())
		should_render = false;

	if (!should_render)
	{
		// TODO: stop/restart video stream
		session.begin_frame();
		session.end_frame(predicted_display_time, {});

		std::unique_lock lock(decoder_mutex);
		for (auto & i: decoders)
		{
			for (auto & frame: i.latest_frames)
				frame.reset();
		}

		return;
	}

	session.begin_frame();

	auto [flags, views] = session.locate_views(viewconfig, predicted_display_time, world_space);
	assert(views.size() == swapchains.size());

	std::array<int, view_count> image_indices;
	assert(views.size() == view_count);
	for (size_t swapchain_index = 0; swapchain_index < views.size(); swapchain_index++)
	{
		int image_index = swapchains[swapchain_index].acquire();
		swapchains[swapchain_index].wait();

		image_indices[swapchain_index] = image_index;
	}

	command_buffer.reset();

	vk::CommandBufferBeginInfo begin_info;
	begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	command_buffer.begin(begin_info);

	// Transition the layout of the decoder framebuffer to the one the decoders expect
	std::vector<vk::ImageMemoryBarrier> image_barriers;
	for (size_t i = 0; i < decoder_output.size(); i++)
	{
		vk::ImageMemoryBarrier barrier
		{
			.srcAccessMask = vk::AccessFlagBits::eNone,
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = shard_accumulator::framebuffer_expected_layout,
			.image = vk::Image{decoder_output[i].image},
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		image_barriers.push_back(barrier);
	}
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlags{}, {}, {}, image_barriers);

	// Keep a reference to the resources needed to blit the images until vkWaitForFences
	std::vector<std::shared_ptr<shard_accumulator::blit_handle>> current_blit_handles;

	std::array<XrPosef, 2> pose = {views[0].pose, views[1].pose};
	std::array<XrFovf, 2> fov = {views[0].fov, views[1].fov};
	{
		std::unique_lock lock(decoder_mutex);
		// Search for the most recent frame available on all decoders.
		// If no such frame exists, use the latest frame for each decoder
		auto common_frame = accumulator_images::common_frame(decoders);
		// Blit images from the decoders
		// TODO be smarter: group blits per eye, so that the framebuffer can use OP_DONT_CARE instead of OP_LOAD and use the same renderpass if possible
		for (auto & i: decoders)
		{
			auto blit_handle = i.frame(common_frame);
			if (not blit_handle)
				continue;

			current_blit_handles.push_back(blit_handle);

			blit_handle->feedback.blitted = application::now();
			blit_handle->feedback.displayed = predicted_display_time;
			blit_handle->feedback.real_pose[0] = views[0].pose;
			blit_handle->feedback.real_pose[1] = views[1].pose;

			pose = blit_handle->view_info.pose;
			fov = blit_handle->view_info.fov;

			int indices[] = {0, 1};
			i.decoder->blit(command_buffer, *blit_handle, indices); // TODO blit indices no longer needed here
		}
	}

	// Transition the output of the decoder to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	image_barriers.clear();
	for (size_t i = 0; i < decoder_output.size(); i++)
	{
		vk::ImageMemoryBarrier barrier{
			.srcAccessMask = vk::AccessFlagBits::eNone,
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.oldLayout = shard_accumulator::framebuffer_expected_layout,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.image = vk::Image{decoder_output[i].image},
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		image_barriers.push_back(barrier);
	}
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, {}, {}, image_barriers);

	// Reproject the image to the real pose
	for (size_t view = 0; view < view_count; view++)
	{
		size_t destination_index = view * swapchains[0].images().size() + image_indices[view];
		reprojector->reproject(command_buffer, view, destination_index, pose[view].orientation, fov[view], views[view].pose.orientation, views[view].fov);
	}

	command_buffer.end();
	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*command_buffer);
	queue.submit(submit_info, *fence);

	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	std::vector<XrCompositionLayerProjectionView> layer_view;
	layer_view.resize(views.size());

	for (size_t swapchain_index = 0; swapchain_index < views.size(); swapchain_index++)
	{
		swapchains[swapchain_index].release();

		layer_view[swapchain_index].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;

		layer_view[swapchain_index].pose = views[swapchain_index].pose;
		layer_view[swapchain_index].fov = views[swapchain_index].fov;

		layer_view[swapchain_index].subImage.swapchain = swapchains[swapchain_index];
		layer_view[swapchain_index].subImage.imageRect.offset = {0, 0};
		layer_view[swapchain_index].subImage.imageRect.extent.width = swapchains[swapchain_index].width();
		layer_view[swapchain_index].subImage.imageRect.extent.height = swapchains[swapchain_index].height();
	}

	float brightness = std::clamp<float>(dbrightness * (predicted_display_time - first_frame_time) / 1.e9, 0, 1);

	XrCompositionLayerColorScaleBiasKHR color_scale_bias{
	        .type = XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR,
	        .colorScale = {brightness, brightness, brightness, 1},
	        .colorBias = {}};

	XrCompositionLayerProjection layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = 0,
	        .space = world_space,
	        .viewCount = (uint32_t)layer_view.size(),
	        .views = layer_view.data(),
	};

	if (instance.has_extension(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME))
		layer.next = &color_scale_bias;

	XrCompositionLayerQuad imgui_layer;
	if (imgui_ctx)
		imgui_layer = plot_performance_metrics(predicted_display_time);

	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));

	if (imgui_ctx)
		layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&imgui_layer));

	session.end_frame(/*timestamp*/ predicted_display_time, layers_base);

	// Network operations may be blocking, do them once everything was submitted
	for (const auto& handle: current_blit_handles)
		send_feedback(handle->feedback);

	if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout");
	device.resetFences(*fence);

	// We don't need those after vkWaitForFences
	current_blit_handles.clear();

	read_actions();
}

void scenes::stream::exit()
{
	exiting = true;
	shard_queue.close();
}

void scenes::stream::setup(const to_headset::video_stream_description & description)
{
	std::unique_lock lock(decoder_mutex);

	// FIXME: stop video thread
	decoders.clear();
	swapchains.clear();

	if (description.items.empty())
	{
		spdlog::info("Stopping video stream");
		return;
	}

	// Create swapchains
	const uint32_t video_width = description.width / view_count;
	const uint32_t video_height = description.height;
	const uint32_t swapchain_width = video_width / description.foveation[0].x.scale;
	const uint32_t swapchain_height = video_height / description.foveation[0].y.scale;

	auto views = system.view_configuration_views(viewconfig);

	swapchains.reserve(views.size());
	for ([[maybe_unused]] auto view: views)
	{
		swapchains.emplace_back(session, device, swapchain_format, swapchain_width, swapchain_height);

		spdlog::info("Created stream swapchain {}: {}x{}", swapchains.size(), swapchain_width, swapchain_height);
	}

	// Create outputs for the decoders
	vk::Extent3D decoder_out_size{video_width, video_height, 1};
	for (size_t i = 0; i < view_count; i++)
	{
		decoder_output[i].format = vk::Format::eA8B8G8R8SrgbPack32;
		decoder_output[i].size.width = video_width;
		decoder_output[i].size.height = video_height;

		vk::ImageCreateInfo image_info{
			.flags = vk::ImageCreateFlags{},
			.imageType = vk::ImageType::e2D,
			.format = vk::Format::eA8B8G8R8SrgbPack32,
			.extent = decoder_out_size,
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eSampled | shard_accumulator::framebuffer_usage,
			.sharingMode = vk::SharingMode::eExclusive,
			.initialLayout = vk::ImageLayout::eUndefined,
		};

		VmaAllocationCreateInfo alloc_info{
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};

		decoder_output[i].image = image_allocation{device, image_info, alloc_info};

		vk::ImageViewCreateInfo image_view_info{
			.image = vk::Image{decoder_output[i].image},
			.viewType = vk::ImageViewType::e2D,
			.format = vk::Format::eA8B8G8R8SrgbPack32,
			.components = {},
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		decoder_output[i].image_view = vk::raii::ImageView(device, image_view_info);
	}

	std::vector<shard_accumulator::blit_target> blit_targets;
	blit_targets.resize(view_count);

	for (size_t i = 0; i < view_count; i++)
	{
		blit_targets[i].image = vk::Image{decoder_output[i].image};
		blit_targets[i].image_view = *decoder_output[i].image_view;
		blit_targets[i].extent.width = video_width;
		blit_targets[i].extent.height = video_height;
		blit_targets[i].offset.x = video_width * i;
		blit_targets[i].offset.y = 0;
	}

	for (const auto & [stream_index, item]: utils::enumerate(description.items))
	{
		spdlog::info("Creating decoder size {}x{} offset {},{}", item.width, item.height, item.offset_x, item.offset_y);

		try
		{
			session.set_refresh_rate(description.fps);
		}
		catch (std::exception & e)
		{
			spdlog::warn("Failed to set refresh rate to {}: {}", description.fps, e.what());
		}

		accumulator_images dec;
		dec.decoder = std::make_unique<shard_accumulator>(device, physical_device, item, description.fps, shared_from_this(), stream_index);
		dec.decoder->set_blit_targets(blit_targets, vk::Format::eA8B8G8R8SrgbPack32);

		decoders.push_back(std::move(dec));
	}

	spdlog::info("Initializing reprojector");
	vk::Extent2D extent = {(uint32_t)swapchains[0].width(), (uint32_t)swapchains[0].height()};
	std::vector<vk::Image> swapchain_images;
	for (auto & swapchain: swapchains)
	{
		for (auto & image: swapchain.images())
			swapchain_images.push_back(image.image);
	}

	std::vector<vk::Image> images;
	for (renderpass_output & i: decoder_output)
	{
		images.push_back(i.image);
	}

	reprojector.emplace(device, physical_device, images, swapchain_images, extent, swapchains[0].format(), description);
}

void scenes::stream::video()
{
#ifdef __ANDROID__
	application::instance().setup_jni();
#endif

	while (not exiting)
	{
		try
		{
			auto shard = shard_queue.pop();

			if (shard.stream_item_idx >= decoders.size())
			{
				// We don't know (yet?) about this stream, ignore packet
				return;
			}
			auto idx = shard.stream_item_idx;
			decoders[idx].decoder->push_shard(std::move(shard));
		}
		catch (utils::sync_queue_closed &)
		{
			break;
		}
		catch (std::exception & e)
		{
			spdlog::error("Exception in video thread: {}", e.what());
			exit();
		}
	}
}

scene::meta& scenes::stream::get_meta_scene()
{
	static meta m{
		"Stream",
		{}
	};

	return m;
}
