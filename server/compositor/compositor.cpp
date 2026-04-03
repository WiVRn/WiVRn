/*
 * WiVRn VR streaming
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

// Copyright 2019-2024, Collabora, Ltd.
// Copyright 2025-2026, NVIDIA CORPORATION.

#include "compositor.h"

// Monado includes
#include "driver/xrt_cast.h"
#include "main/comp_frame.h"
#include "util/comp_render_helpers.h"
#include "util/comp_vulkan.h"
#include "util/u_debug.h"
#include "util/u_handles.h"
#include "util/u_time.h"
#include "vk/vk_helpers.h"

#include "driver/wivrn_session.h"
#include "encoder/video_encoder.h"
#include "utils/method.h"

DEBUG_GET_ONCE_LOG_OPTION(log, "XRT_COMPOSITOR_LOG", U_LOGGING_INFO)

namespace details
{
template <auto Method, typename Result, typename... Args>
struct method_trait<Method, Result (wivrn::compositor::*)(Args...)>
{
	static Result magic(xrt_compositor * arg, Args... args)
	{
		return std::invoke(
		        Method,
		        static_cast<wivrn::compositor *>(reinterpret_cast<struct comp_base *>(arg)),
		        args...);
	}
};

} // namespace details

namespace
{
os_mutex copy_mutex(std::mutex & m)
{
	return {
	        .mutex = *m.native_handle(),
#ifndef NDEBUG
	        .initialized = true,
	        .recursive = false,
#endif
	};
}

const comp_swapchain_image & get_layer_image(const comp_layer & layer, uint32_t swapchain_index, uint32_t image_index)
{
	return reinterpret_cast<struct comp_swapchain *>(comp_layer_get_swapchain(&layer, swapchain_index))->images[image_index];
}

std::array<vk::Format, 3> image_formats(int bit_depth)
{
	switch (bit_depth)
	{
		case 8:
			return {
			        vk::Format::eR8Unorm,
			        vk::Format::eR8G8Unorm,
			        vk::Format::eG8B8R82Plane420Unorm,
			};
		case 10:
			return {
			        vk::Format::eR16Unorm,
			        vk::Format::eR16G16Unorm,
			        vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16,
			};
	}
	throw std::runtime_error(std::format("Unsupported bit depth {}", bit_depth));
}

std::array<wivrn::compositor::image, 2> make_images(wivrn::vk_bundle & vk, vk::CommandPool command_pool, std::span<wivrn::encoder_settings> encoders)
{
	auto formats = image_formats(encoders[0].bit_depth);

	vk::StructureChain image_info{
	        vk::ImageCreateInfo{
	                .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
	                .imageType = vk::ImageType::e2D,
	                .format = formats.back(),
	                .extent = {
	                        .width = encoders[0].width,
	                        .height = encoders[0].height,
	                        .depth = 1,
	                },
	                .mipLevels = 1,
	                .arrayLayers = 3, // left, right then alpha
	                .samples = vk::SampleCountFlagBits::e1,
	                .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
	        },
	        vk::ImageFormatListCreateInfo{
	                .viewFormatCount = formats.size(),
	                .pViewFormats = formats.data(),
	        },
	};
#if WIVRN_USE_VULKAN_ENCODE
	if (
	        std::get<vk::PhysicalDeviceVideoMaintenance1FeaturesKHR>(vk.feat).videoMaintenance1 and
	        std::ranges::contains(
	                encoders,
	                wivrn::encoder_vulkan,
	                &wivrn::encoder_settings::encoder_name))
	{
		image_info.get().flags |= vk::ImageCreateFlagBits::eVideoProfileIndependentKHR;
		image_info.get().usage |= vk::ImageUsageFlagBits::eVideoEncodeSrcKHR;
	}
#endif

	auto bufs = vk.device.allocateCommandBuffers({
	        .commandPool = command_pool,
	        .commandBufferCount = 2,
	});

	auto make_image = [&](int i) {
		vk::ImageViewUsageCreateInfo usage{
		        .usage = vk::ImageUsageFlagBits::eStorage,
		};
		image_allocation image{
		        vk.device,
		        image_info.get(),
		        VmaAllocationCreateInfo{.usage = VMA_MEMORY_USAGE_AUTO},
		        std::format("compositor YCbCr image {}", i),
		};
		vk::Image vk_image{image};
		return wivrn::compositor::image{
		        .sem{vk.device, vk::SemaphoreCreateInfo{}},
		        .fence{vk.device, vk::FenceCreateInfo{}},
		        .cmd{std::move(bufs[i])},
		        .image{std::move(image)},
		        .view_y{
		                vk.device,
		                {
		                        .pNext = &usage,
		                        .image = vk_image,
		                        .viewType = vk::ImageViewType::e2DArray,
		                        .format = formats[0],
		                        .subresourceRange = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                                .levelCount = 1,
		                                .layerCount = vk::RemainingArrayLayers,
		                        },
		                },
		        },
		        .view_cbcr{
		                vk.device,
		                {
		                        .pNext = &usage,
		                        .image = vk_image,
		                        .viewType = vk::ImageViewType::e2DArray,
		                        .format = formats[1],
		                        .subresourceRange = {
		                                .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                                .levelCount = 1,
		                                .layerCount = vk::RemainingArrayLayers,
		                        },
		                },
		        }};
	};

	return {make_image(0), make_image(1)};
}

vk::raii::Fence make_fence(wivrn::vk_bundle & vk, const char * name)
{
	vk::raii::Fence res{
	        vk.device,
	        vk::FenceCreateInfo{
	                .flags = vk::FenceCreateFlagBits::eSignaled,
	        },
	};
	vk.name(res, name);
	return res;
}

vk::Extent3D render_extent(const wivrn::from_headset::headset_info_packet & info)
{
	return {
	        .width = info.render_eye_width,
	        .height = info.render_eye_height,
	        .depth = 1,
	};
}

} // namespace

namespace wivrn
{

xrt_result_t compositor::predict_frame(int64_t * out_frame_id,
                                       int64_t * out_wake_time_ns,
                                       int64_t * out_predicted_gpu_time_ns,
                                       int64_t * out_predicted_display_time_ns,
                                       int64_t * out_predicted_display_period_ns)
{
	int64_t frame_id = -1;
	int64_t wake_up_time_ns = 0;
	int64_t present_slop_ns = 0;
	int64_t desired_present_time_ns = 0;
	int64_t predicted_display_time_ns = 0;
	pacer.predict(
	        frame_id,
	        wake_up_time_ns,
	        desired_present_time_ns,
	        present_slop_ns,
	        predicted_display_time_ns);

	frame.waited.id = frame_id;
	frame.waited.desired_present_time_ns = desired_present_time_ns;
	frame.waited.present_slop_ns = present_slop_ns;
	frame.waited.predicted_display_time_ns = predicted_display_time_ns;

	*out_frame_id = frame_id;
	*out_wake_time_ns = wake_up_time_ns;
	*out_predicted_gpu_time_ns = desired_present_time_ns; // Not quite right but close enough.
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_predicted_display_period_ns = pacer.get_frame_duration();
	return XRT_SUCCESS;
}

xrt_result_t compositor::mark_frame(int64_t frame_id,
                                    xrt_compositor_frame_point point,
                                    int64_t when_ns)
{
	switch (point)
	{
		case XRT_COMPOSITOR_FRAME_POINT_WOKE:
			session.dump_time("wake_up", frame_id, when_ns);
			return XRT_SUCCESS;
		default:
			assert(false);
	}
	return XRT_ERROR_VULKAN;
}

xrt_result_t compositor::layer_commit(xrt_graphics_sync_handle_t sync_handle)
{
	u_graphics_sync_unref(&sync_handle);

	// Move waited frame to rendering frame, clear waited.
	comp_frame_move_and_clear_locked(&frame.rendering, &frame.waited);

	U_LOG_IFL_D(log_level, "frame %ld commit %d layers", frame.rendering.id, layer_accum.layer_count);

	auto _ = vk.device.waitForFences(*fence, true, INT64_MAX);

	if (encode_request >= 0 // encoders have not picked up the previous frame
	    or layer_accum.layer_count == 0 or not session.connected())
	{
		comp_frame_clear_locked(&frame.rendering);
		return XRT_SUCCESS;
	}

	int i = acquire_image();
	if (i < 0)
	{
		comp_frame_clear_locked(&frame.rendering);
		return XRT_SUCCESS;
	}
	assert(images[i].fence.getStatus() == vk::Result::eNotReady);

	auto & view_info = images[i].view_info;
	images[i].frame_index = frame.rendering.id;
	view_info = {
	        .display_time = session.get_offset().to_headset(frame.rendering.predicted_display_time_ns),
	        .alpha = layer_accum.data.env_blend_mode == XRT_BLEND_MODE_ALPHA_BLEND,
	};

	session.dump_time("begin", frame.rendering.id, os_monotonic_get_ns());

	cmd.reset();
	cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	bool flip_y = false;
	std::array<vk::ImageView, 2> src;
	std::array<xrt_rect, 2> src_rect;
	std::array<xrt_fov, 2> src_fov;

	// Check if we can pass a layer directly to foveation
	if (layer_accum.layer_count == 1 and
	    (layer_accum.layers[0].data.type == XRT_LAYER_PROJECTION or
	     layer_accum.layers[0].data.type == XRT_LAYER_PROJECTION_DEPTH))
	{
		const auto & layer = layer_accum.layers[0];
		for (int view = 0; view < 2; ++view)
		{
			const auto & data = (layer.data.type == XRT_LAYER_PROJECTION ? layer.data.proj.v : layer.data.depth.v)[view];
			src[view] = get_image_view(
			        &get_layer_image(layer, view, data.sub.image_index),
			        layer.data.flags,
			        data.sub.array_index);
			src_rect[view] = data.sub.rect;
			src_fov[view] = data.fov;
			flip_y = layer.data.flip_y;
			view_info.pose[view] = xrt_cast(data.pose);
			view_info.fov[view] = xrt_cast(data.fov);
		}
	}
	else
	{
		// no fast-path, squash layers
		std::array<xrt_pose, 2> poses;
		std::tie(poses, src_fov, src_rect) = squasher.do_layers(
		        vk.device,
		        cmd,
		        session.get_hmd(),
		        pacer.get_frame_duration(),
		        frame.rendering,
		        layer_accum);

		src = squasher.get_views();

		for (int view = 0; view < 2; ++view)
		{
			view_info.pose[view] = xrt_cast(poses[view]);
			view_info.fov[view] = xrt_cast(src_fov[view]);
		}
	}

	vk::ImageMemoryBarrier2 target_barrier{
	        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
	        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eGeneral,
	        .image = images[i].image,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = 0,
	                .layerCount = vk::RemainingArrayLayers,
	        },
	};

	cmd.pipelineBarrier2({
	        .imageMemoryBarrierCount = 1,
	        .pImageMemoryBarriers = &target_barrier,
	});

	if (session.get_info().eye_gaze)
	{
		auto now = os_monotonic_get_ns();
		session.add_tracking_request(device_id::EYE_GAZE, frame.rendering.desired_present_time_ns, now, now);
	}
	view_info.foveation = foveation.foveate(
	        vk.device,
	        cmd,
	        images[i].view_y,
	        images[i].view_cbcr,
	        flip_y,
	        src,
	        src_rect,
	        src_fov);

	if (std::ranges::any_of(encoders, &video_encoder::need_copy))
	{
		target_barrier.srcAccessMask = target_barrier.dstAccessMask;
		target_barrier.srcStageMask = target_barrier.dstStageMask;
		target_barrier.oldLayout = target_barrier.newLayout;
		target_barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
		target_barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
		target_barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;

		cmd.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &target_barrier,
		});
	}
	cmd.end();

	vk.device.resetFences(*fence);
	{
		std::unique_lock lock{vk.queue_mutex};
		vk.queue.submit(
		        vk::SubmitInfo{
		                .commandBufferCount = 1,
		                .pCommandBuffers = &*cmd,
		                .signalSemaphoreCount = 1,
		                .pSignalSemaphores = &*images[i].sem,
		        },
		        *fence);
	}

	images[i].cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	auto info = pacer.present_to_info(frame.rendering.desired_present_time_ns);

	bool need_queue_transfer = false;
	std::vector<vk::Semaphore> present_done_sem;
	for (auto & encoder: encoders)
	{
		if (encoder->stream_idx == 2 and not view_info.alpha)
			continue;
		auto [transfer, sem] = encoder->present_image(
		        images[i].image,
		        need_queue_transfer,
		        images[i].cmd,
		        info.frame_id);
		need_queue_transfer |= transfer;
		if (sem)
			present_done_sem.push_back(sem);
	}

	vk::PipelineStageFlags stages = vk::PipelineStageFlagBits::eAllCommands;

	vk::SubmitInfo submit_info{
	        .waitSemaphoreCount = 1,
	        .pWaitSemaphores = &*images[i].sem,
	        .pWaitDstStageMask = &stages,
	};

#if WIVRN_USE_VULKAN_ENCODE
	if (need_queue_transfer)
	{
		vk::ImageMemoryBarrier2 video_barrier{
		        .srcStageMask = vk::PipelineStageFlagBits2KHR::eTransfer,
		        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
		        .dstStageMask = vk::PipelineStageFlagBits2KHR::eVideoEncodeKHR,
		        .oldLayout = target_barrier.newLayout,
		        .newLayout = vk::ImageLayout::eVideoEncodeSrcKHR,
		        .srcQueueFamilyIndex = vk.queue_family_index,
		        .dstQueueFamilyIndex = vk.encode_queue_family_index,
		        .image = images[i].image,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = vk::RemainingArrayLayers,
		        },
		};
		images[i].cmd.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &video_barrier,
		});
	}
	submit_info.setSignalSemaphores(present_done_sem);
#endif
	images[i].cmd.end();
	submit_info.setCommandBuffers(*images[i].cmd);

	{
		std::unique_lock lock{vk.queue_mutex};
		vk.queue.submit(submit_info, *images[i].fence);
		for (auto & encoder: encoders)
		{
			if (encoder->stream_idx == 2 and not view_info.alpha)
				continue;
			encoder->post_submit();
		}
	}

	auto j = encode_request.exchange(i);
	encode_request.notify_all();
	assert(j == -1);

	// Now is a good point to garbage collect.
	comp_swapchain_shared_garbage_collect(&cscs);

	return XRT_SUCCESS;
}

xrt_result_t compositor::get_display_refresh_rate(float * hz)
{
	*hz = refresh_rate;
	return XRT_SUCCESS;
}

xrt_result_t compositor::request_display_refresh_rate(float hz)
{
	try
	{
		session.send_control(to_headset::refresh_rate_change{.fps = hz});
	}
	catch (std::exception & e)
	{
		U_LOG_W("refresh rate change failed: %s", e.what());
	}
	return XRT_SUCCESS;
}

int compositor::acquire_image()
{
	for (auto [i, image]: std::ranges::enumerate_view(images))
	{
		if (not image.busy.exchange(true))
			return i;
	}
	return -1;
}

void compositor::encoder_work(std::stop_token tok)
{
	while (not tok.stop_requested())
	{
		auto req = encode_request.exchange(-1);
		if (req < 0)
		{
			encode_request.wait(req);
			continue;
		}

		assert(req < images.size());
		auto & image = images[req];

		auto _ = vk.device.waitForFences(*image.fence, true, UINT64_MAX);
		vk.device.resetFences(*image.fence);

		try
		{
			for (auto & encoder: encoders)
			{
				if (encoder->stream_idx < 2 or image.view_info.alpha)
					encoder->encode(session, image.view_info, image.frame_index);
			}
		}
		catch (std::exception & e)
		{
			U_LOG_W("encode error: %s", e.what());
		}
		image.busy = false;
	}
}

void compositor::send_video_stream_description()

{
	to_headset::video_stream_description desc{
	        .width = uint16_t(images[0].image.info().extent.width),
	        .height = uint16_t(images[0].image.info().extent.height),
	        .fps = settings[0].fps,
	};
	static_assert(std::tuple_size_v<decltype(settings)> == std::tuple_size_v<decltype(desc.codec)>);
	std::ranges::transform(settings, desc.codec.begin(), &encoder_settings::codec);
	session.send_control(std::move(desc));
}

compositor::compositor(wivrn_session & session) :
        comp_base{
                .base = {
                        .base = {
                                .info{},
                                .begin_session = method_pointer<&compositor::begin_session>,
                                .end_session = method_pointer<&compositor::end_session>,
                                .predict_frame = method_pointer<&compositor::predict_frame>,
                                .mark_frame = method_pointer<&compositor::mark_frame>,
                                .begin_frame = method_pointer<&compositor::begin_frame>,
                                .discard_frame = method_pointer<&compositor::discard_frame>,
                                .layer_commit = method_pointer<&compositor::layer_commit>,
                                .get_display_refresh_rate = method_pointer<&compositor::get_display_refresh_rate>,
                                .request_display_refresh_rate = method_pointer<&compositor::request_display_refresh_rate>,
                                .destroy = method_pointer<&compositor::destroy>,
                        },
                },
        },
        log_level(debug_get_log_option_log()),
        session(session),
        cmd_pool(vk.device, vk::CommandPoolCreateInfo{
                                    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                    .queueFamilyIndex = vk.queue_family_index,
                            }),
        settings(get_encoder_settings(vk, session)),
        images{make_images(vk, cmd_pool, settings)},
        cmd{std::move(vk.device.allocateCommandBuffers({.commandPool = *cmd_pool, .commandBufferCount = 1})[0])},
        fence{make_fence(vk, "compositor fence")},
        refresh_rate(settings[0].fps),
        pacer(U_TIME_1S_IN_NS / refresh_rate),
        squasher(vk, render_extent(session.get_info())),
        foveation(vk, images[0].image.info().extent),
        encoder_thread{[&](std::stop_token t) { encoder_work(t); }}
{
	comp_base * c_base = this;
	// Ensure we can safely cast pointers
	assert(intptr_t(&base) == intptr_t(this));
	auto res = vk_init_from_given(
	        &c_base->vk,
	        (PFN_vkGetInstanceProcAddr)vk.instance.getProcAddr("vkGetInstanceProcAddr"),
	        *vk.instance,
	        *vk.physical_device,
	        *vk.device,
	        vk.queue_family_index,
	        0,
	        vk.has_device_ext(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME),
	        vk.has_device_ext(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME),
	        std::get<vk::PhysicalDeviceVulkan12Features>(vk.feat).timelineSemaphore,
	        true, // in Vulkan 1.2
	        vk.has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME),
	        log_level);
	vk::detail::resultCheck(vk::Result(res), "vk_init_from_given");

	// vk_init_from_given assumes a graphics queue was provided
	c_base->vk.graphics_queue = nullptr;

	if (c_base->vk.main_queue)
		c_base->vk.main_queue->mutex = copy_mutex(vk.queue_mutex);
	if (c_base->vk.encode_queue)
		c_base->vk.encode_queue->mutex = copy_mutex(vk.encode_queue_mutex);

	{
		comp_vulkan_formats formats{};
		comp_vulkan_formats_check(&c_base->vk, &formats);
		comp_vulkan_formats_copy_to_info(&formats, &base.base.info);
		comp_vulkan_formats_log(log_level, &formats);
	}

	comp_base_init(this);

	// Tie the lifetimes of swapchains to Vulkan.
	xrt_result_t xret = comp_swapchain_shared_init(&cscs, &c_base->vk);
	if (xret != XRT_SUCCESS)
		throw std::runtime_error("comp_swapchain_shared_init failed");

	print_encoders(settings);
	for (auto [i, settings]: std::ranges::enumerate_view(settings))
		encoders[i] = video_encoder::create(vk, settings, i);
	send_video_stream_description();
}

compositor::~compositor()
{
	encoder_thread.request_stop();
	encode_request = -2;
	encode_request.notify_all();
	comp_base * c_base = this;
	comp_swapchain_shared_garbage_collect(&cscs);
	comp_swapchain_shared_destroy(&cscs, &c_base->vk);
	comp_base_fini(this);
}

xrt_system_compositor_info compositor::sys_info() const
{
	const auto & info = session.get_info();
	auto [prop, dev_id] = vk.physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
	xrt_system_compositor_info res{
	        .view_config_count = 1,
	        .view_configs = {
	                {
	                        .view_type = XRT_VIEW_TYPE_STEREO,
	                        .view_count = 2,
	                        .views = {},
	                },
	        },
	        .max_layers = squasher.max_layers(prop.properties),
	        .supported_blend_modes = {
	                XRT_BLEND_MODE_OPAQUE,
	                XRT_BLEND_MODE_ALPHA_BLEND,
	        },
	        .supported_blend_mode_count = uint8_t(1 + info.passthrough),
	        .refresh_rate_count = std::min<uint32_t>(info.available_refresh_rates.size(), std::size(res.refresh_rates_hz)),
	        .supports_fov_mutable = true,
	};

	std::ranges::copy(dev_id.deviceUUID, res.compositor_vk_deviceUUID.data);
	std::ranges::copy(dev_id.deviceUUID, res.client_vk_deviceUUID.data);
	std::ranges::copy(std::span(info.available_refresh_rates).subspan(0, res.refresh_rate_count), res.refresh_rates_hz);

	const auto extent = render_extent(info);
	for (auto & view: std::span(res.view_configs[0].views, res.view_configs[0].view_count))
	{
		view = {
		        .recommended = {
		                .width_pixels = extent.width,
		                .height_pixels = extent.height,
		                .sample_count = 1,
		        },
		        .max = {
		                .width_pixels = extent.width * 2u,
		                .height_pixels = extent.height * 2u,
		                .sample_count = 1,
		        },
		};
	}
	return res;
}

void compositor::set_bitrate(uint32_t bitrate)
{
	for (auto & encoder: encoders)
		encoder->set_bitrate(bitrate);
}

void compositor::set_refresh_rate(float hz)
{
	U_LOG_IFL_D(log_level, "Refresh rate change from %.0f to %.0f", refresh_rate.load(), hz);
	refresh_rate = hz;
	pacer.set_frame_duration(U_TIME_1S_IN_NS / hz);
	for (auto & encoder: encoders)
		encoder->set_framerate(hz);
}

void compositor::update_tracking(const from_headset::tracking & tracking)
{
	foveation.update_tracking(tracking);
}

void compositor::update_foveation_center_override(const from_headset::override_foveation_center & center)
{
	foveation.update_foveation_center_override(center);
}

void compositor::resume()
{
	for (auto & encoder: encoders)
		encoder->reset();
	send_video_stream_description();
}

void compositor::on_feedback(const from_headset::feedback & feedback, const clock_offset & o)
{
	uint8_t stream = feedback.stream_index;
	if (stream >= encoders.size())
		return;
	encoders[stream]->on_feedback(feedback);
	if (not o)
		return;
	pacer.on_feedback(feedback, o);
}

} // namespace wivrn
