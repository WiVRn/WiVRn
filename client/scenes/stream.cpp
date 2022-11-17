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
#include "decoder/shard_accumulator.h"
#include "external/magic_enum.hpp"
#include "glm/fwd.hpp"
#include "spdlog/spdlog.h"
#include "utils/check.h"
#include "utils/ranges.h"
#include "utils/sync_queue.h"
#include "vk/device_memory.h"
#include "vk/image.h"
#include "wivrn_packets.h"
#include "xr/details/enumerate.h"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vulkan/vulkan_core.h>

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

std::shared_ptr<scenes::stream> scenes::stream::create(std::unique_ptr<wivrn_session> network_session)
{
	std::shared_ptr<stream> self{new stream};

	self->network_session = std::move(network_session);

	from_headset::headset_info_packet info{};

	info.recommended_eye_width = self->swapchains[0].width();
	info.recommended_eye_height = self->swapchains[0].height();

	if (self->instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		info.available_refresh_rates = self->session.get_refresh_rates();
		info.preferred_refresh_rate = self->session.get_current_refresh_rate();
	}

	if (info.available_refresh_rates.empty())
		spdlog::warn("Unable to detect refresh rates");

	self->network_session->send_control(info);

	self->network_thread = std::thread(&stream::process_packets, self.get());
	pthread_setname_np(self->network_thread.native_handle(), "network_thread");

	self->video_thread = std::thread(&stream::video, self.get());
	pthread_setname_np(self->video_thread.native_handle(), "video_thread");

	self->command_buffer = self->commandpool.allocate_command_buffer();
	self->fence = self->create_fence(false);

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

	return self;
}

scenes::stream::~stream()
{
	cleanup();
	exiting = true;
	shard_queue.close();

	video_thread.join();
	if (tracking_thread)
		tracking_thread->join();
	network_thread.join();
}

void scenes::stream::push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle)
{
	std::lock_guard lock(decoder_mutex);

	if (!application::is_visible())
		return;

	for (auto & i: decoders)
	{
		if (i.decoder.get() == decoder)
		{
			static_assert(std::tuple_size_v<decltype(i.latest_frames)> == 2);
			std::swap(i.latest_frames[0], i.latest_frames[1]);
			i.latest_frames[1] = handle;
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

void scenes::stream::render()
{
	if (exiting)
		application::pop_scene();

	XrFrameState framestate = session.wait_frame();
	// auto t0 = application::now();

	if (decoders.empty())
		framestate.shouldRender = false;

	if (!framestate.shouldRender)
	{
		// TODO: stop/restart video stream
		session.begin_frame();
		session.end_frame(framestate.predictedDisplayTime, {});

		std::unique_lock lock(decoder_mutex);
		for (auto & i: decoders)
		{
			for (auto & frame: i.latest_frames)
				frame.reset();
		}

		return;
	}

	session.begin_frame();
	// auto dt2 = application::now() - t0;

	auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);
	assert(views.size() == swapchains.size());

	std::array<int, view_count> image_indices;
	assert(views.size() == view_count);
	for (size_t swapchain_index = 0; swapchain_index < views.size(); swapchain_index++)
	{
		int image_index = swapchains[swapchain_index].acquire();
		swapchains[swapchain_index].wait();

		image_indices[swapchain_index] = image_index;
	}

	CHECK_VK(vkResetCommandBuffer(command_buffer, 0));

	VkCommandBufferBeginInfo begin_info{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	CHECK_VK(vkBeginCommandBuffer(command_buffer, &begin_info));

	// Transition the layout of the decoder framebuffer to the one the decoders expect
	std::vector<VkImageMemoryBarrier> image_barriers;
	for (size_t i = 0; i < decoder_output.size(); i++)
	{
		image_barriers.push_back(VkImageMemoryBarrier{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		        .srcAccessMask = 0,
		        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		        .newLayout = shard_accumulator::framebuffer_expected_layout,
		        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .image = decoder_output[i].image,
		        .subresourceRange = {
		                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		});
	}

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, image_barriers.size(), image_barriers.data());

	// Keep a reference to the resources needed to blit the images until vkWaitForFences
	std::vector<std::shared_ptr<shard_accumulator::blit_handle>> current_blit_handles;

	std::array<XrPosef, 2> pose = {views[0].pose, views[1].pose};
	std::array<XrFovf, 2> fov = {views[0].fov, views[1].fov};
	{
		std::lock_guard lock(decoder_mutex);
		// Search for the most recent frame available on all decoders.
		// If no such frame exists, use the most latest frame for each decoder
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
			blit_handle->feedback.displayed = framestate.predictedDisplayTime;
			blit_handle->feedback.real_pose[0] = views[0].pose;
			blit_handle->feedback.real_pose[1] = views[1].pose;

			pose = blit_handle->view_info.pose;
			fov = blit_handle->view_info.fov;

			send_feedback(blit_handle->feedback);

			int indices[] = {0, 1};
			i.decoder->blit(command_buffer, *blit_handle, indices); // TODO blit indices no longer needed here
		}
	}

	// Transition the output of the decoder to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	image_barriers.clear();
	for (size_t i = 0; i < decoder_output.size(); i++)
	{
		image_barriers.push_back(VkImageMemoryBarrier{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		        .srcAccessMask = 0,
		        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		        .oldLayout = shard_accumulator::framebuffer_expected_layout,
		        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .image = decoder_output[i].image,
		        .subresourceRange = {
		                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		});
	}
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, image_barriers.size(), image_barriers.data());

	// Reproject the image to the real pose
	for (size_t view = 0; view < view_count; view++)
	{
		size_t destination_index = view * swapchains[0].images().size() + image_indices[view];
		reprojector.reproject(command_buffer, view, destination_index, pose[view].orientation, fov[view], views[view].pose.orientation, views[view].fov);
	}

	CHECK_VK(vkEndCommandBuffer(command_buffer));
	VkSubmitInfo submit_info{
	        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	        .commandBufferCount = 1,
	        .pCommandBuffers = &command_buffer,
	};
	CHECK_VK(vkQueueSubmit(queue, 1, &submit_info, fence));
	// auto dt4 = application::now() - t0;

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

	float brightness = std::clamp<float>(dbrightness * (framestate.predictedDisplayTime - first_frame_time) / 1.e9, 0, 1);

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

	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));
	session.end_frame(/*timestamp*/ framestate.predictedDisplayTime, layers_base);

	// auto dt5 = application::now() - t0;
	CHECK_VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	CHECK_VK(vkResetFences(device, 1, &fence));

	// We don't need those after vkWaitForFences
	current_blit_handles.clear();

	// auto dt6 = application::now() - t0;
	read_actions();

	// spdlog::info("Render timing: begin frame {:.3f}ms, submit commands {:.3f}ms, end frame {:.3f}ms, wait fence {:.3f}ms, predicted display time {:.3f}ms", dt2/1.e6, dt4/1.e6, dt5/1.e6, dt6/1.e6, (framestate.predictedDisplayTime - t0) / 1.e6);
}

void scenes::stream::cleanup()
{
	// Assumes decoder_mutex is locked
	ready_ = false;
	decoders.clear();

	for (auto & output: decoder_output)
	{
		if (output.image)
			vkDestroyImage(device, output.image, nullptr);

		if (output.image_view)
			vkDestroyImageView(device, output.image_view, nullptr);

		if (output.memory)
			vkFreeMemory(device, output.memory, nullptr);

		output.image = VK_NULL_HANDLE;
		output.image_view = VK_NULL_HANDLE;
		output.memory = VK_NULL_HANDLE;
	}
}

void scenes::stream::setup(const to_headset::video_stream_description & description)
{
	std::unique_lock lock(decoder_mutex);

	cleanup();

	if (description.items.empty())
	{
		spdlog::info("Stopping video stream");
		return;
	}

	// Create outputs for the decoders
	const uint32_t width = description.width / view_count;
	const uint32_t height = description.height;

	VkExtent3D decoder_out_size{width, height, 1};
	for (size_t i = 0; i < view_count; i++)
	{
		decoder_output[i].format = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
		decoder_output[i].size = {
		        .width = width,
		        .height = height};

		VkImageCreateInfo image_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		        .flags = 0,
		        .imageType = VK_IMAGE_TYPE_2D,
		        .format = VK_FORMAT_A8B8G8R8_SRGB_PACK32,
		        .extent = decoder_out_size,
		        .mipLevels = 1,
		        .arrayLayers = 1,
		        .samples = VK_SAMPLE_COUNT_1_BIT,
		        .tiling = VK_IMAGE_TILING_OPTIMAL,
		        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | shard_accumulator::framebuffer_usage,
		        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		decoder_output[i].image = vk::image{device, image_info}.release();

		decoder_output[i].memory = vk::device_memory{device, physical_device, decoder_output[i].image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}.release();

		VkImageViewCreateInfo image_view_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		        .image = decoder_output[i].image,
		        .viewType = VK_IMAGE_VIEW_TYPE_2D,
		        .format = VK_FORMAT_A8B8G8R8_SRGB_PACK32,
		        .components = {}, // IDENTITY by default
		        .subresourceRange = {
		                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1}};

		CHECK_VK(vkCreateImageView(device, &image_view_info, nullptr, &decoder_output[i].image_view));
	}

	std::vector<shard_accumulator::blit_target> blit_targets;
	blit_targets.resize(view_count);

	for (size_t i = 0; i < view_count; i++)
	{
		blit_targets[i].image = decoder_output[i].image;
		blit_targets[i].image_view = decoder_output[i].image_view;
		blit_targets[i].extent = {width, height};
		blit_targets[i].offset = {static_cast<int>(width * i), 0};
	}

	for (const auto & [stream_index, item]: utils::enumerate(description.items))
	{
		spdlog::info("Creating decoder size {}x{} offset {},{}", item.width, item.height, item.offset_x, item.offset_y);

		accumulator_images dec;
		dec.decoder = std::make_unique<shard_accumulator>(device, physical_device, item, description.fps, shared_from_this(), stream_index);
		dec.decoder->set_blit_targets(blit_targets, VK_FORMAT_A8B8G8R8_SRGB_PACK32);

		decoders.push_back(std::move(dec));
	}

	spdlog::info("Initializing reprojector");
	VkExtent2D extent = {swapchains[0].width(), swapchains[0].height()};
	std::vector<VkImage> swapchain_images;
	for (auto & swapchain: swapchains)
	{
		for (auto & image: swapchain.images())
			swapchain_images.push_back(image.image);
	}

	std::vector<VkImage> images;
	for (renderpass_output & i: decoder_output)
	{
		images.push_back(i.image);
	}

	reprojector.init(device, physical_device, images, swapchain_images, extent, swapchains[0].format());
}

void scenes::stream::video()
{
	jni_thread jni;

	while (not exiting)
	{
		try
		{
			auto shard = shard_queue.pop();

			std::visit(
			        [this](auto & shard) {
				        if (shard.stream_item_idx >= decoders.size())
				        {
					        // We don't know (yet?) about this stream, ignore packet
					        return;
				        }
				        auto idx = shard.stream_item_idx;
				        decoders[idx].decoder->push_shard(std::move(shard));
			        },
			        shard);
		}
		catch (utils::sync_queue_closed &)
		{
			break;
		}
		catch (std::exception & e)
		{
			spdlog::error("Exception in video thread: {}", e.what());
			exiting = true;
		}
	}
}
