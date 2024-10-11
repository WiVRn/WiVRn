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
#define GLM_FORCE_RADIANS

#include "stream.h"

#include "application.h"
#include "audio/audio.h"
#include "boost/pfr/core.hpp"
#include "decoder/shard_accumulator.h"
#include "hardware.h"
#include "spdlog/spdlog.h"
#include "utils/named_thread.h"
#include "utils/ranges.h"
#include "vk/pipeline.h"
#include "vk/shader.h"
#include "wivrn_packets.h"
#include <algorithm>
#include <mutex>
#include <ranges>
#include <thread>
#include <vulkan/vulkan_raii.hpp>

using namespace wivrn;

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
                vk::Format::eB8G8R8A8Srgb};

std::shared_ptr<scenes::stream> scenes::stream::create(std::unique_ptr<wivrn_session> network_session, float guessed_fps)
{
	std::shared_ptr<stream> self{new stream};
	self->network_session = std::move(network_session);

	from_headset::headset_info_packet info{};

	auto view = self->system.view_configuration_views(self->viewconfig)[0];
	view = override_view(view, guess_model());

	auto resolution_scale = application::get_config().resolution_scale;

	view.recommendedImageRectWidth *= resolution_scale;
	view.recommendedImageRectHeight *= resolution_scale;

	info.recommended_eye_width = view.recommendedImageRectWidth;
	info.recommended_eye_height = view.recommendedImageRectHeight;

	auto [flags, views] = self->session.locate_views(
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	        self->instance.now(),
	        application::space(xr::spaces::view));

	assert(views.size() == info.fov.size());

	for (auto [i, j]: std::views::zip(views, info.fov))
	{
		j = i.fov;
	}

	const auto & config = application::get_config();

	if (self->instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		info.available_refresh_rates = self->session.get_refresh_rates();
		if (std::ranges::find(info.available_refresh_rates, config.preferred_refresh_rate) != info.available_refresh_rates.end())
			info.preferred_refresh_rate = config.preferred_refresh_rate;
		else
			info.preferred_refresh_rate = self->session.get_current_refresh_rate();
	}

	if (info.preferred_refresh_rate == 0)
	{
		spdlog::warn("Unable to detect preferred refresh rate, using {}", guessed_fps);
		info.preferred_refresh_rate = guessed_fps;
	}

	if (info.available_refresh_rates.empty())
		spdlog::warn("Unable to detect refresh rates");

	info.hand_tracking = application::get_hand_tracking_supported();
	info.eye_gaze = config.check_feature(feature::eye_gaze);
	info.face_tracking2_fb = config.check_feature(feature::face_tracking);
	info.palm_pose = application::space(xr::spaces::palm_left) or application::space(xr::spaces::palm_right);

	audio::get_audio_description(info);
	if (not(config.check_feature(feature::microphone)))
		info.microphone = {};

	info.supported_codecs = decoder_impl::supported_codecs();

	self->network_session->send_control(info);

	self->network_thread = utils::named_thread("network_thread", &stream::process_packets, self.get());

	self->command_buffer = std::move(self->device.allocateCommandBuffers({
	        .commandPool = *self->commandpool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);

	self->fence = self->device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});

	// Look up the XrActions for haptics
	self->haptics_actions[0].action = application::get_action("/user/hand/left/output/haptic").first;
	self->haptics_actions[0].path = application::string_to_path("/user/hand/left");

	self->haptics_actions[1].action = application::get_action("/user/hand/right/output/haptic").first;
	self->haptics_actions[1].path = application::string_to_path("/user/hand/right");

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

	self->query_pool = vk::raii::QueryPool(
	        self->device,
	        vk::QueryPoolCreateInfo{
	                .queryType = vk::QueryType::eTimestamp,
	                .queryCount = size_gpu_timestamps,
	        });

	return self;
}

void scenes::stream::on_focused()
{
	if (application::get_config().show_performance_metrics)
	{
		swapchain_imgui = xr::swapchain(
		        session,
		        device,
		        swapchain_format,
		        1500,
		        1000);

		imgui_ctx.emplace(physical_device,
		                  device,
		                  queue_family_index,
		                  queue,
		                  application::space(xr::spaces::view),
		                  std::span<imgui_context::controller>{},
		                  swapchain_imgui,
		                  glm::vec2{1.0, 0.6666});

		imgui_ctx->set_position({0, 0, -1}, {1, 0, 0, 0});
		plots_toggle_1 = get_action("plots_toggle_1").first;
		plots_toggle_2 = get_action("plots_toggle_2").first;
	}

	wifi = application::get_wifi_lock().get_wifi_lock();
	assert(video_stream_description);
	setup_reprojection_swapchain();
}

void scenes::stream::on_unfocused()
{
	imgui_ctx.reset();
	swapchain_imgui = xr::swapchain();
	wifi.reset();
}

scenes::stream::~stream()
{
	exit();

	if (tracking_thread && tracking_thread->joinable())
		tracking_thread->join();

	if (network_thread.joinable())
		network_thread.join();
}

void scenes::stream::push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle)
{
	assert(handle);
	if (!application::is_visible())
		return;

	{
		std::shared_lock lock(decoder_mutex);
		std::unique_lock frame_lock(frames_mutex);
		const auto stream = handle->feedback.stream_index;
		if (stream < decoders.size())
		{
			assert(decoder == decoders[stream].decoder.get());
			handle->feedback.received_from_decoder = application::now();
			std::swap(handle, decoders[stream].latest_frames[handle->feedback.frame_index % decoders[stream].latest_frames.size()]);
		}

		if (state_ != state::streaming && std::all_of(decoders.begin(), decoders.end(), [](accumulator_images & i) {
			    return i.latest_frames.back();
		    }))
		{
			state_ = state::streaming;
			spdlog::info("Stream scene ready at t={}", application::now());
		}
	}

	if (handle and not handle->feedback.blitted)
	{
		send_feedback(handle->feedback);
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

std::vector<std::shared_ptr<shard_accumulator::blit_handle>> scenes::stream::common_frame(XrTime display_time)
{
	if (decoders.empty())
		return {};
	std::unique_lock lock(frames_mutex);
	thread_local std::vector<shard_accumulator::blit_handle *> common_frames;
	common_frames.clear();
	for (size_t i = 0; i < decoders.size(); ++i)
	{
		if (i == 0)
		{
			for (const auto & h: decoders[i].latest_frames)
				if (h)
					common_frames.push_back(h.get());
		}
		else
		{
			// clang-format off
			std::erase_if(common_frames,
				[this, i](auto & left)
				{
					return std::ranges::none_of(
						decoders[i].latest_frames,
						[&left](auto & right)
						{
							return left->feedback.frame_index == right->feedback.frame_index;
						});
				});
			// clang-format on
		}
	}
	std::optional<uint64_t> frame_index;
	if (not common_frames.empty())
	{
		auto min = std::ranges::min_element(common_frames,
		                                    std::ranges::less{},
		                                    [display_time](auto frame) {
			                                    if (not frame)
				                                    return std::numeric_limits<XrTime>::max();
			                                    return std::abs(frame->view_info.display_time - display_time);
		                                    });

		assert(*min);
		frame_index = (*min)->feedback.frame_index;
	}
	else
	{
		spdlog::warn("Failed to find a common frame for all decoders, dumping available frames per decoder");
		for (const auto & decoder: decoders)
		{
			std::string frames;
			for (const auto & frame: decoder.latest_frames)
			{
				if (frame)
					frames += " " + std::to_string(frame->feedback.frame_index);
				else
					frames += " -";
			}
			spdlog::warn(frames);
		}
	}
	std::vector<std::shared_ptr<shard_accumulator::blit_handle>> result;
	result.reserve(decoders.size());
	for (const auto & decoder: decoders)
		result.push_back(decoder.frame(frame_index));
	return result;
}

std::shared_ptr<shard_accumulator::blit_handle> scenes::stream::accumulator_images::frame(std::optional<uint64_t> id) const
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

void scenes::stream::render(const XrFrameState & frame_state)
{
	if (exiting)
		application::pop_scene();

	display_time_phase = frame_state.predictedDisplayTime % frame_state.predictedDisplayPeriod;
	display_time_period = frame_state.predictedDisplayPeriod;

	std::shared_lock lock(decoder_mutex);
	if (decoders.empty() or not frame_state.shouldRender)
	{
		// TODO: stop/restart video stream
		session.begin_frame();
		session.end_frame(frame_state.predictedDisplayTime, {});

		std::unique_lock lock(frames_mutex);
		for (auto & i: decoders)
		{
			for (auto & frame: i.latest_frames)
				frame.reset();
		}

		return;
	}

	if (state_ == state::stalled)
		application::pop_scene();

	assert(not swapchains.empty());
	for (auto & i: decoders)
	{
		if (auto sampler = i.decoder->sampler(); sampler and not *i.blit_pipeline)
		{
			// Create blit pipeline
			// Create VkDescriptorSetLayout with an immutable sampler
			vk::DescriptorSetLayoutBinding sampler_layout_binding{
			        .binding = 0,
			        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			        .descriptorCount = 1,
			        .stageFlags = vk::ShaderStageFlagBits::eFragment,
			        .pImmutableSamplers = &sampler,
			};

			vk::DescriptorSetLayoutCreateInfo layout_info{
			        .bindingCount = 1,
			        .pBindings = &sampler_layout_binding,
			};

			i.descriptor_set_layout = vk::raii::DescriptorSetLayout(device, layout_info);
			i.descriptor_set = device.allocateDescriptorSets(
			                                 vk::DescriptorSetAllocateInfo{
			                                         .descriptorPool = *blit_descriptor_pool,
			                                         .descriptorSetCount = 1,
			                                         .pSetLayouts = &*i.descriptor_set_layout,
			                                 })[0]
			                           .release();

			const auto & description = i.decoder->desc();
			vk::Extent2D image_size = i.decoder->image_size();
			std::array useful_size{
			        float(description.width) / image_size.width,
			        float(description.height) / image_size.height,
			};
			spdlog::info("useful size: {}x{} with buffer {}x{}",
			             description.width,
			             description.height,
			             image_size.width,
			             image_size.height);

			std::array specialization_constants_desc{
			        vk::SpecializationMapEntry{
			                .constantID = 0,
			                .offset = 0,
			                .size = sizeof(float),
			        },
			        vk::SpecializationMapEntry{
			                .constantID = 1,
			                .offset = sizeof(float),
			                .size = sizeof(float),
			        }};

			vk::SpecializationInfo vert_specialization_info;
			vert_specialization_info.setMapEntries(specialization_constants_desc);
			vert_specialization_info.setData<float>(useful_size);

			VkBool32 do_srgb = need_srgb_conversion(guess_model());
			vk::SpecializationMapEntry frag_specialization_constant_desc{
			        .constantID = 0,
			        .offset = 0,
			        .size = sizeof(do_srgb),
			};
			vk::SpecializationInfo frag_specialization_info;
			frag_specialization_info.setMapEntries(frag_specialization_constant_desc);
			frag_specialization_info.setData<VkBool32>(do_srgb);

			// Create graphics pipeline
			vk::raii::ShaderModule vertex_shader = load_shader(device, "stream.vert");
			vk::raii::ShaderModule fragment_shader = load_shader(device, "stream.frag");

			vk::PipelineLayoutCreateInfo pipeline_layout_info{
			        .setLayoutCount = 1,
			        .pSetLayouts = &*i.descriptor_set_layout,
			};

			i.blit_pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

			vk::pipeline_builder pipeline_info{
			        .flags = {},
			        .Stages = {{
			                           .stage = vk::ShaderStageFlagBits::eVertex,
			                           .module = *vertex_shader,
			                           .pName = "main",
			                           .pSpecializationInfo = &vert_specialization_info,
			                   },
			                   {
			                           .stage = vk::ShaderStageFlagBits::eFragment,
			                           .module = *fragment_shader,
			                           .pName = "main",
			                           .pSpecializationInfo = &frag_specialization_info,
			                   }},
			        .VertexInputState = {.flags = {}},
			        .VertexBindingDescriptions = {},
			        .VertexAttributeDescriptions = {},
			        .InputAssemblyState = {{
			                .topology = vk::PrimitiveTopology::eTriangleStrip,
			        }},
			        .ViewportState = {.flags = {}},
			        .Viewports = {{}},
			        .Scissors = {{}},
			        .RasterizationState = {{
			                .polygonMode = vk::PolygonMode::eFill,
			                .lineWidth = 1,
			        }},
			        .MultisampleState = {{
			                .rasterizationSamples = vk::SampleCountFlagBits::e1,
			        }},
			        .ColorBlendState = {.flags = {}},
			        .ColorBlendAttachments = {{.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB}},
			        .DynamicState = {.flags = {}},
			        .DynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
			        .layout = *i.blit_pipeline_layout,
			        .renderPass = *blit_render_pass,
			        .subpass = 0,
			};

			i.blit_pipeline = vk::raii::Pipeline(device, application::get_pipeline_cache(), pipeline_info);
		}
	}

	if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout");

	device.resetFences(*fence);

	// We don't need those after vkWaitForFences
	current_blit_handles.clear();

	gpu_timestamps timestamps;
	if (query_pool_filled)
	{
		auto [res, timestamps2] = query_pool.getResults<uint64_t>(
		        0,
		        size_gpu_timestamps,
		        size_gpu_timestamps * sizeof(uint64_t),
		        sizeof(uint64_t),
		        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

		if (res == vk::Result::eSuccess)
		{
			boost::pfr::for_each_field(timestamps, [n = 1, &timestamps2](float & t) mutable {
				t = (timestamps2[n++] - timestamps2[0]) * application::get_physical_device_properties().limits.timestampPeriod / 1e9;
			});
		}
	}

	session.begin_frame();

	std::array<int, view_count> image_indices;
	for (size_t swapchain_index = 0; swapchain_index < view_count; swapchain_index++)
	{
		int image_index = swapchains[swapchain_index].acquire();
		swapchains[swapchain_index].wait();

		image_indices[swapchain_index] = image_index;
	}

	command_buffer.reset();

	vk::CommandBufferBeginInfo begin_info;
	begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	command_buffer.begin(begin_info);

	// Keep a reference to the resources needed to blit the images until vkWaitForFences
	std::vector<std::shared_ptr<shard_accumulator::blit_handle>> blit_handles;

	command_buffer.resetQueryPool(*query_pool, 0, size_gpu_timestamps);
	command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, 0);

	std::array<XrPosef, 2> pose{};
	std::array<XrFovf, 2> fov{};
	std::array<wivrn::to_headset::foveation_parameter, 2> foveation{};
	{
		// Search for frame with desired display time on all decoders
		// If no such frame exists, use the latest frame for each decoder
		blit_handles = common_frame(frame_state.predictedDisplayTime);

		// Blit images from the decoders
		for (auto [i, blit_handle]: std::views::zip(decoders, blit_handles))
		{
			if (not blit_handle)
				continue;

			blit_handle->feedback.blitted = application::now();
			if (blit_handle->feedback.blitted - blit_handle->feedback.received_from_decoder > 1'000'000'000)
				state_ = stream::state::stalled;
			++blit_handle->feedback.times_displayed;
			blit_handle->feedback.displayed = frame_state.predictedDisplayTime;

			pose = blit_handle->view_info.pose;
			fov = blit_handle->view_info.fov;
			foveation = blit_handle->view_info.foveation;

			vk::DescriptorImageInfo image_info{
			        .imageView = *blit_handle->image_view,
			        .imageLayout = vk::ImageLayout::eGeneral,
			};

			vk::WriteDescriptorSet descriptor_write{
			        .dstSet = i.descriptor_set,
			        .dstBinding = 0,
			        .dstArrayElement = 0,
			        .descriptorCount = 1,
			        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
			        .pImageInfo = &image_info,
			};

			device.updateDescriptorSets(descriptor_write, {});
			if (*blit_handle->current_layout != vk::ImageLayout::eGeneral)
			{
				vk::ImageMemoryBarrier barrier{
				        .srcAccessMask = vk::AccessFlagBits::eNone,
				        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
				        .oldLayout = *blit_handle->current_layout,
				        .newLayout = vk::ImageLayout::eGeneral,
				        .image = blit_handle->image,
				        .subresourceRange = {
				                .aspectMask = vk::ImageAspectFlagBits::eColor,
				                .levelCount = 1,
				                .layerCount = 1,
				        },
				};

				command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, barrier);
				*blit_handle->current_layout = vk::ImageLayout::eGeneral;
			}
		}
	}

	uint16_t x_offset = 0;
	for (auto & out: decoder_output)
	{
		command_buffer.beginRenderPass(
		        {
		                .renderPass = *blit_render_pass,
		                .framebuffer = *out.frame_buffer,
		                .renderArea = {
		                        .offset = {0, 0},
		                        .extent = out.size,
		                },
		                .clearValueCount = 0,
		        },
		        vk::SubpassContents::eInline);

		for (const auto & decoder: decoders)
		{
			if (not *decoder.blit_pipeline)
				continue;

			command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *decoder.blit_pipeline);

			const auto & description = decoder.decoder->desc();
			int x0 = description.offset_x - x_offset;
			int y0 = description.offset_y;
			int x1 = x0 + description.width;
			int y1 = y0 + description.height;

			vk::Viewport viewport{
			        .x = (float)x0,
			        .y = (float)y0,
			        .width = (float)description.width,
			        .height = (float)description.height,
			        .minDepth = 0,
			        .maxDepth = 1,
			};

			x0 = std::clamp<int>(x0, 0, out.size.width);
			x1 = std::clamp<int>(x1, 0, out.size.width);
			y0 = std::clamp<int>(y0, 0, out.size.height);
			y1 = std::clamp<int>(y1, 0, out.size.height);

			vk::Rect2D scissor{
			        .offset = {.x = x0, .y = y0},
			        .extent = {.width = (uint32_t)(x1 - x0), .height = (uint32_t)(y1 - y0)},
			};

			command_buffer.setViewport(0, viewport);
			command_buffer.setScissor(0, scissor);

			command_buffer.bindDescriptorSets(
			        vk::PipelineBindPoint::eGraphics,
			        *decoder.blit_pipeline_layout,
			        0,
			        decoder.descriptor_set,
			        nullptr);
			command_buffer.draw(3, 1, 0, 0);
		}
		command_buffer.endRenderPass();
		x_offset += out.size.width;
	}

	command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *query_pool, 1);
	reprojector->set_foveation(foveation);

	// Unfoveate the image to the real pose
	for (size_t view = 0; view < view_count; view++)
	{
		size_t destination_index = view * swapchains[0].images().size() + image_indices[view];
		reprojector->reproject(command_buffer, view, destination_index);
	}

	command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *query_pool, 2);

	command_buffer.end();
	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*command_buffer);
	queue.submit(submit_info, *fence);

	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	std::vector<XrCompositionLayerProjectionView> layer_view(view_count);

	for (size_t swapchain_index = 0; swapchain_index < view_count; swapchain_index++)
	{
		swapchains[swapchain_index].release();

		layer_view[swapchain_index] =
		        {
		                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		                .pose = pose[swapchain_index],
		                .fov = fov[swapchain_index],

		                .subImage = {
		                        .swapchain = swapchains[swapchain_index],
		                        .imageRect = {
		                                .offset = {0, 0},
		                                .extent = {
		                                        swapchains[swapchain_index].width(),
		                                        swapchains[swapchain_index].height(),
		                                },
		                        },
		                },
		        };
	}

	XrCompositionLayerProjection layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = 0,
	        .space = application::space(xr::spaces::world),
	        .viewCount = (uint32_t)layer_view.size(),
	        .views = layer_view.data(),
	};

	XrCompositionLayerQuad imgui_layer;
	if (imgui_ctx and plots_visible)
	{
		accumulate_metrics(frame_state.predictedDisplayTime, blit_handles, timestamps);
		imgui_layer = plot_performance_metrics(frame_state.predictedDisplayTime);
	}

	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));

	if (imgui_ctx and plots_visible)
		layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&imgui_layer));

	try
	{
		session.end_frame(frame_state.predictedDisplayTime, layers_base);
	}
	catch (std::system_error & e)
	{
		if (e.code().category() == xr::error_category() and e.code().value() == XR_ERROR_POSE_INVALID)
			spdlog::info("Invalid pose submitted");
		else
			throw;
	}

	// Network operations may be blocking, do them once everything was submitted
	{
		std::vector<serialization_packet> packets;
		packets.reserve(blit_handles.size());
		for (const auto & handle: blit_handles)
		{
			if (handle)
			{
				auto & packet = packets.emplace_back();
				wivrn_session::control_socket_t::serialize(packet, handle->feedback);
			}
		}
		if (not packets.empty())
		{
			try
			{
				network_session->send_control(std::span(packets));
			}
			catch (std::exception & e)
			{
				spdlog::warn("Exception while sending feedback packet: {}", e.what());
			}
		}
	}

	read_actions();

	if (plots_toggle_1 and plots_toggle_2)
	{
		XrActionStateGetInfo get_info{
		        .type = XR_TYPE_ACTION_STATE_GET_INFO,
		        .action = plots_toggle_1,
		};

		XrActionStateBoolean state_1{XR_TYPE_ACTION_STATE_BOOLEAN};
		CHECK_XR(xrGetActionStateBoolean(session, &get_info, &state_1));
		get_info.action = plots_toggle_2;
		XrActionStateBoolean state_2{XR_TYPE_ACTION_STATE_BOOLEAN};
		CHECK_XR(xrGetActionStateBoolean(session, &get_info, &state_2));

		if (state_1.currentState and state_2.currentState and (state_1.changedSinceLastSync or state_2.changedSinceLastSync))
			plots_visible = not plots_visible;
	}

	query_pool_filled = true;
}

void scenes::stream::exit()
{
	exiting = true;
}

void scenes::stream::setup(const to_headset::video_stream_description & description)
{
	std::unique_lock lock(decoder_mutex);

	decoders.clear();

	if (description.items.empty())
	{
		spdlog::info("Stopping video stream");
		return;
	}

	video_stream_description = description;

	const uint32_t video_width = description.width / view_count;
	const uint32_t video_height = description.height;

	// Create renderpass
	{
		vk::AttachmentDescription color_desc{
		        .format = vk::Format::eA8B8G8R8SrgbPack32,
		        .samples = vk::SampleCountFlagBits::e1,
		        .loadOp = vk::AttachmentLoadOp::eDontCare,
		        .storeOp = vk::AttachmentStoreOp::eStore,
		        .initialLayout = vk::ImageLayout::eUndefined,
		        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};

		vk::AttachmentReference color_ref{
		        .attachment = 0,
		        .layout = vk::ImageLayout::eColorAttachmentOptimal,
		};

		vk::SubpassDescription subpass{
		        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		};
		subpass.setColorAttachments(color_ref);

		vk::RenderPassCreateInfo renderpass_info{
		        .flags = {},
		};
		renderpass_info.setAttachments(color_desc);
		renderpass_info.setSubpasses(subpass);

		blit_render_pass = vk::raii::RenderPass(device, renderpass_info);
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
		        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
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

		decoder_output[i].frame_buffer = vk::raii::Framebuffer(
		        device,
		        vk::FramebufferCreateInfo{
		                .renderPass = *blit_render_pass,
		                .attachmentCount = 1,
		                .pAttachments = &*decoder_output[i].image_view,
		                .width = decoder_out_size.width,
		                .height = decoder_out_size.height,
		                .layers = 1,
		        });
	}

	{
		vk::DescriptorPoolSize pool_size{
		        .type = vk::DescriptorType::eCombinedImageSampler,
		        .descriptorCount = uint32_t(description.items.size()),
		};
		blit_descriptor_pool = vk::raii::DescriptorPool(
		        device,
		        vk::DescriptorPoolCreateInfo{
		                .maxSets = uint32_t(description.items.size()),
		                .poolSizeCount = 1,
		                .pPoolSizes = &pool_size,
		        });
	}

	for (const auto & [stream_index, item]: utils::enumerate(description.items))
	{
		spdlog::info("Creating decoder size {}x{} offset {},{}", item.width, item.height, item.offset_x, item.offset_y);

		if (instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
		{
			try
			{
				session.set_refresh_rate(description.fps);
			}
			catch (std::exception & e)
			{
				spdlog::warn("Failed to set refresh rate to {}: {}", description.fps, e.what());
			}
		}

		accumulator_images dec;
		dec.decoder = std::make_unique<shard_accumulator>(device, physical_device, item, description.fps, shared_from_this(), stream_index);

		decoders.push_back(std::move(dec));
	}
}

void scenes::stream::setup_reprojection_swapchain()
{
	std::unique_lock lock(decoder_mutex);

	swapchains.clear();
	const uint32_t video_width = video_stream_description->width / view_count;
	const uint32_t video_height = video_stream_description->height;
	const uint32_t swapchain_width = video_width / video_stream_description->foveation[0].x.scale;
	const uint32_t swapchain_height = video_height / video_stream_description->foveation[0].y.scale;

	auto views = system.view_configuration_views(viewconfig);

	swapchains.reserve(views.size());
	for (auto view: views)
	{
		XrExtent2Di extent{
		        .width = std::min<int32_t>(view.maxImageRectWidth, swapchain_width),
		        .height = std::min<int32_t>(view.maxImageRectHeight, swapchain_height),
		};
		swapchains.emplace_back(session, device, swapchain_format, extent.width, extent.height);

		spdlog::info("Created stream swapchain {}: {}x{}", swapchains.size(), extent.width, extent.height);
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

	reprojector.emplace(device, physical_device, images, swapchain_images, extent, swapchains[0].format(), *video_stream_description);
}

scene::meta & scenes::stream::get_meta_scene()
{
	static meta m{
	        .name = "Stream",
	        .actions = {
	                {"plots_toggle_1", XR_ACTION_TYPE_BOOLEAN_INPUT},
	                {"plots_toggle_2", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        },
	        .bindings = {
	                suggested_binding{
	                        "/interaction_profiles/oculus/touch_controller",
	                        {
	                                {"plots_toggle_1", "/user/hand/left/input/thumbstick/click"},
	                                {"plots_toggle_2", "/user/hand/right/input/thumbstick/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/bytedance/pico_neo3_controller",
	                        {
	                                {"plots_toggle_1", "/user/hand/left/input/thumbstick/click"},
	                                {"plots_toggle_2", "/user/hand/right/input/thumbstick/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/bytedance/pico4_controller",
	                        {
	                                {"plots_toggle_1", "/user/hand/left/input/thumbstick/click"},
	                                {"plots_toggle_2", "/user/hand/right/input/thumbstick/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/htc/vive_focus3_controller",
	                        {
	                                {"plots_toggle_1", "/user/hand/left/input/thumbstick/click"},
	                                {"plots_toggle_2", "/user/hand/right/input/thumbstick/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/khr/simple_controller",
	                        {},
	                },
	        },
	};

	return m;
}

void scenes::stream::on_reference_space_changed(XrReferenceSpaceType space, XrTime when)
{
	if (space == XrReferenceSpaceType::XR_REFERENCE_SPACE_TYPE_LOCAL)
	{
		recenter_requested = true;
	}
}
