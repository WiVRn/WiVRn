/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_comp_target.h"

#include "driver/wivrn_session.h"
#include "encoder/video_encoder.h"
#include "util/u_logging.h"
#include "utils/method.h"
#include "utils/scoped_lock.h"
#include "wivrn_config.h"
#include "wivrn_foveation.h"

#include "main/comp_compositor.h"
#include "math/m_space.h"
#include "xrt_cast.h"

#include <algorithm>
#include <ranges>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>

#include "xrt/xrt_config_build.h" // IWYU pragma: keep
#ifdef XRT_FEATURE_RENDERDOC
#include "renderdoc_app.h"
#endif

namespace wivrn
{
std::vector<const char *> wivrn_comp_target::wanted_instance_extensions = {};
std::vector<const char *> wivrn_comp_target::wanted_device_extensions = {
// For FFMPEG
#ifdef VK_EXT_external_memory_dma_buf
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
#endif
#ifdef VK_EXT_image_drm_format_modifier
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
#endif

// For vulkan video encode
#ifdef VK_KHR_video_queue
        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_queue
        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_maintenance1
        VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_h264
        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_h265
        VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME,
#endif
};

#ifdef XRT_FEATURE_RENDERDOC
static auto renderdoc()
{
	auto x = []() {
		RENDERDOC_API_1_5_0 * rdoc_api = nullptr;
		const char * env = std::getenv("ENABLE_VULKAN_RENDERDOC_CAPTURE");
		if (not env or env != std::string_view("1"))
			return rdoc_api;
		void * mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
		if (mod)
		{
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
			XRT_MAYBE_UNUSED int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&rdoc_api);
			assert(ret == 1);
		}
		return rdoc_api;
	};
	static auto res = x();
	return res;
}
#endif

void wivrn_comp_target::init_semaphores()
{
	auto vk = get_vk();
	VkResult ret;

	fini_semaphores();

	VkSemaphoreCreateInfo info = {
	        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	ret = vk->vkCreateSemaphore(vk->device, &info, nullptr, &semaphores.present_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	semaphores.render_complete_is_timeline = false;
	ret = vk->vkCreateSemaphore(vk->device, &info, nullptr, &semaphores.render_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}
}
void wivrn_comp_target::fini_semaphores()
{
	auto vk = get_vk();

	if (semaphores.present_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, semaphores.present_complete, nullptr);
		semaphores.present_complete = VK_NULL_HANDLE;
	}

	if (semaphores.render_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, semaphores.render_complete, nullptr);
		semaphores.render_complete = VK_NULL_HANDLE;
	}
}

VkResult wivrn_comp_target::create_images_impl(vk::ImageUsageFlags flags)
{
	auto vk = get_vk();
	assert(image_count > 0);
	COMP_DEBUG(c, "Creating %d images.", image_count);
	auto & device = wivrn_bundle->device;

	destroy_images();

	auto format = vk::Format(this->format);

	bool is_10bit = format == vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16;

	std::array formats = {
	        is_10bit ? vk::Format::eR16Unorm : vk::Format::eR8Unorm,
	        is_10bit ? vk::Format::eR16G16Unorm : vk::Format::eR8G8Unorm,
	        format};

	images = U_TYPED_ARRAY_CALLOC(comp_target_image, image_count);

	vk::StructureChain image_info{
	        vk::ImageCreateInfo{
	                .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
	                .imageType = vk::ImageType::e2D,
	                .format = format,
	                .extent = {
	                        .width = width,
	                        .height = height,
	                        .depth = 1,
	                },
	                .mipLevels = 1,
	                .arrayLayers = 3, // left, right then alpha
	                .samples = vk::SampleCountFlagBits::e1,
	                .tiling = vk::ImageTiling::eOptimal,
	                .usage = flags | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
	                .sharingMode = vk::SharingMode::eExclusive,
	        },
	        vk::ImageFormatListCreateInfo{
	                .viewFormatCount = formats.size(),
	                .pViewFormats = formats.data(),
	        },
	};
#if WIVRN_USE_VULKAN_ENCODE
	if (
	        vk->features.video_maintenance_1 and
	        std::ranges::contains(
	                settings,
	                encoder_vulkan,
	                &encoder_settings::encoder_name))
	{
		image_info.get().flags |= vk::ImageCreateFlagBits::eVideoProfileIndependentKHR;
		image_info.get().usage |= vk::ImageUsageFlagBits::eVideoEncodeSrcKHR;
	}
#endif

	psc.images.resize(image_count);
	for (uint32_t i = 0; i < image_count; i++)
	{
		auto & image = psc.images[i].image;
		image = image_allocation(
		        device,
		        image_info.get(),
		        {.usage = VMA_MEMORY_USAGE_AUTO},
		        std::format("comp target image {}", i));
		images[i].handle = image;
	}

	for (uint32_t i = 0; i < image_count; i++)
	{
		auto & item = psc.images[i];
		vk::ImageViewUsageCreateInfo usage{
		        .usage = flags,
		};
		item.image_view_y = vk::raii::ImageView(device,
		                                        {
		                                                .pNext = &usage,
		                                                .image = item.image,
		                                                .viewType = vk::ImageViewType::e2DArray,
		                                                .format = formats[0],
		                                                .subresourceRange = {
		                                                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                                                        .levelCount = 1,
		                                                        .layerCount = vk::RemainingArrayLayers,
		                                                },
		                                        });
		item.image_view_cbcr = vk::raii::ImageView(device,
		                                           {
		                                                   .pNext = &usage,
		                                                   .image = item.image,
		                                                   .viewType = vk::ImageViewType::e2DArray,
		                                                   .format = formats[1],
		                                                   .subresourceRange = {
		                                                           .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                                                           .levelCount = 1,
		                                                           .layerCount = vk::RemainingArrayLayers,
		                                                   },
		                                           });
		images[i].view = VkImageView(*item.image_view_y);
		images[i].view_cbcr = VkImageView(*item.image_view_cbcr);
		wivrn_bundle->name(item.image_view_y, "comp target image view (y)");
		wivrn_bundle->name(item.image_view_cbcr, "comp target image view (CbCr)");
	}

	psc.fence = vk::raii::Fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
	wivrn_bundle->name(psc.fence, "comp target fence");

	psc.command_buffer = std::move(device.allocateCommandBuffers(
	        {.commandPool = *command_pool,
	         .commandBufferCount = 1})[0]);
	wivrn_bundle->name(psc.command_buffer, "comp target command buffer");

	return VK_SUCCESS;
}
void wivrn_comp_target::destroy_images()
{
	if (images == nullptr)
		return;

	if (wivrn_bundle)
		wivrn_bundle->device.waitIdle();

	psc.status = 1;
	psc.status.notify_all();
	encoder_threads.clear();
	encoders.clear();

	psc.images.clear();

	free(images);
	images = nullptr;

	fini_semaphores();
}

void wivrn_comp_target::create_encoders()
{
	auto vk = get_vk();
	assert(encoders.empty());
	assert(encoder_threads.empty());
	assert(wivrn_bundle);
	psc.status = 0;

	desc.width = width;
	desc.height = height;

	std::map<int, std::vector<std::shared_ptr<video_encoder>>> thread_params;

	for (auto [i, settings]: std::ranges::enumerate_view(settings))
	{
		auto & encoder = encoders.emplace_back(
		        video_encoder::create(*wivrn_bundle, settings, i));
		desc.codec[i] = settings.codec;

		thread_params[settings.group].emplace_back(encoder);
	}

	for (auto & [group, params]: thread_params)
	{
		auto & thread = encoder_threads.emplace_back([this](auto stop_token, auto... args) { return run_present(stop_token, args...); }, encoder_threads.size(), std::move(params));
		std::string name = "encoder " + std::to_string(group);
		pthread_setname_np(thread.native_handle(), name.c_str());
	}
	cnx.send_control(to_headset::video_stream_description{desc});
}

bool wivrn_comp_target::init_pre_vulkan()
{
	return true;
}
bool wivrn_comp_target::init_post_vulkan(uint32_t preferred_width, uint32_t preferred_height)
{
	auto vk = get_vk();
	try
	{
		wivrn_bundle.emplace(*vk,
		                     wivrn_comp_target::wanted_instance_extensions,
		                     wivrn_comp_target::wanted_device_extensions);
		command_pool = vk::raii::CommandPool(
		        wivrn_bundle->device,
		        {
		                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		                .queueFamilyIndex = vk->main_queue->family_index,
		        });
		wivrn_bundle->name(command_pool, "comp target command pool");
	}
	catch (std::exception & e)
	{
		U_LOG_E("Compositor target init failed: %s", e.what());
		return false;
	}

	try
	{
		settings = get_encoder_settings(
		        *wivrn_bundle,
		        cnx.get_info(),
		        *cnx.get_settings());
		print_encoders(settings);

		c->settings.preferred.width = settings[0].width;
		c->settings.preferred.height = settings[0].height;
	}
	catch (const std::exception & e)
	{
		wivrn_ipc_socket_monado->send(from_monado::server_error{
		        .where = "Error creating encoder",
		        .message = e.what(),
		});
		U_LOG_E("Failed to create video encoder: %s", e.what());
		return false;
	}
	cnx.set_foveated_size(c->settings.preferred.width, c->settings.preferred.height);
	foveation.emplace(*wivrn_bundle, *cnx.get_hmd().hmd);

	return true;
}

bool wivrn_comp_target::check_ready()
{
	if (not cnx.connected())
		return false;

	return true;
}

void wivrn_comp_target::create_images(const comp_target_create_images_info * create_info, vk_bundle_queue * present_queue)
{
	assert(present_queue != nullptr);
	(void)present_queue;

	// Free old images.
	destroy_images();

	init_semaphores();

	// will fail on encoder init if bit_depth is arbitrary garbage
	if (settings[0].bit_depth == 10)
		format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
	else
		format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

	width = create_info->extent.width;
	height = create_info->extent.height;
	surface_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

	image_count = 3;
	color_space = create_info->color_space;

	VkResult res = create_images_impl(vk::ImageUsageFlags(create_info->image_usage));
	if (res != VK_SUCCESS)
	{
		vk_print_result(get_vk(), __FILE__, __LINE__, __func__, res, "create_images");
		// TODO
		abort();
	}
	create_encoders();
}

bool wivrn_comp_target::has_images()
{
	return images;
}

VkResult wivrn_comp_target::acquire(uint32_t * out_index)
{
	while (true)
	{
		for (uint32_t i = 0; i < image_count; i++)
		{
			if (psc.images[i].status == pseudo_swapchain::status_t::free)
			{
				psc.images[i].status = pseudo_swapchain::status_t::acquired;
				*out_index = i;
				return VK_SUCCESS;
			}
		}
	};
}

void wivrn_comp_target::run_present(std::stop_token stop_token, int index, std::vector<std::shared_ptr<video_encoder>> encoders)
{
	auto & vk = *wivrn_bundle;
	U_LOG_I("Starting encoder thread %d", index);

	const uint8_t status_bit = 1 << (index + 1);

	while (not stop_token.stop_requested())
	{
		{
			auto status = psc.status.load();
			// Bit 0 to request exit
			if (status & 1)
				return;

			if (not(status & status_bit))
			{
				psc.status.wait(status);
				continue;
			}
		}

		// Get local copies before releasing the image
		auto view_info = psc.view_info;
		auto frame_index = psc.frame_index;

		auto res = vk.device.waitForFences(*psc.fence, true, UINT64_MAX);

		try
		{
			for (auto & encoder: encoders)
			{
				if (encoder->stream_idx < 2 or view_info.alpha)
					encoder->encode(cnx, view_info, frame_index);
			}
		}
		catch (std::exception & e)
		{
			U_LOG_W("encode error: %s", e.what());
		}
		catch (...)
		{
			// Ignore errors
		}

		// Update encoder status, release image
		if ((psc.status &= ~status_bit) == 0)
		{
			psc.status.notify_all();
			for (auto & img: psc.images)
			{
				if (img.status == pseudo_swapchain::status_t::encoding)
				{
					img.status = pseudo_swapchain::status_t::free;
					break;
				}
			}
		}
	}
}

VkResult wivrn_comp_target::present(
        vk_bundle_queue * present_queue,
        uint32_t index,
        uint64_t timeline_semaphore_value,
        int64_t desired_present_time_ns,
        int64_t present_slop_ns)
{
	assert(present_queue != nullptr);
	(void)present_queue;

	assert(index < image_count);
	assert(psc.images[index].status == pseudo_swapchain::status_t::acquired);

	auto vk = get_vk();
	auto res = wivrn_bundle->device.waitForFences(*psc.fence, true, UINT64_MAX);

	vk::Semaphore wait_semaphore(semaphores.render_complete);
	vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTransfer;
	vk::SubmitInfo submit_info{
	        .waitSemaphoreCount = 1,
	        .pWaitSemaphores = &wait_semaphore,
	        .pWaitDstStageMask = &wait_stage,
	};

	if (c->base.layer_accum.layer_count == 0 or not cnx.get_offset() or skip_encoding)
	{
		scoped_lock lock(vk->main_queue->mutex);
		wivrn_bundle->queue.submit(submit_info);
		psc.images[index].status = pseudo_swapchain::status_t::free;
		return VK_SUCCESS;
	}

	auto & command_buffer = psc.command_buffer;
	auto & psc_image = psc.images[index];

	// Wait for encoders to be done with previous frame
	for (auto status = psc.status.load(); status != 0; status = psc.status)
		psc.status.wait(status);

	command_buffer.reset();
	command_buffer.begin(vk::CommandBufferBeginInfo{
	        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	});

	wivrn_bundle->device.resetFences(*psc.fence);
	psc_image.status = pseudo_swapchain::status_t::encoding;
	auto info = pacer.present_to_info(desired_present_time_ns);
	const bool do_alpha = c->base.layer_accum.data.env_blend_mode == XRT_BLEND_MODE_ALPHA_BLEND;

	bool need_queue_transfer = false;
	std::vector<vk::Semaphore> present_done_sem;
	for (auto & encoder: encoders)
	{
		if (encoder->stream_idx == 2 and not do_alpha)
			continue;
		auto [transfer, sem] = encoder->present_image(
		        psc_image.image,
		        need_queue_transfer,
		        command_buffer,
		        info.frame_id);
		need_queue_transfer |= transfer;
		if (sem)
			present_done_sem.push_back(sem);
	}

#if WIVRN_USE_VULKAN_ENCODE
	if (need_queue_transfer)
	{
		vk::ImageMemoryBarrier2 video_barrier{
		        .srcStageMask = vk::PipelineStageFlagBits2KHR::eTransfer,
		        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
		        .dstStageMask = vk::PipelineStageFlagBits2KHR::eVideoEncodeKHR,
		        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
		        .newLayout = vk::ImageLayout::eVideoEncodeSrcKHR,
		        .srcQueueFamilyIndex = vk->main_queue->family_index,
		        .dstQueueFamilyIndex = vk->encode_queue->family_index,
		        .image = psc_image.image,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = vk::RemainingArrayLayers},
		};
		command_buffer.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &video_barrier,
		});
	}
	submit_info.setSignalSemaphores(present_done_sem);
#endif
	command_buffer.end();
	submit_info.setCommandBuffers(*command_buffer);

	{
		scoped_lock lock(vk->main_queue->mutex);
		wivrn_bundle->queue.submit(submit_info, *psc.fence);
		for (auto & encoder: encoders)
		{
			if (encoder->stream_idx == 2 and not do_alpha)
				continue;
			encoder->post_submit();
		}
	}

#ifdef XRT_FEATURE_RENDERDOC
	if (auto r = renderdoc())
		r->EndFrameCapture(nullptr, nullptr);
#endif

	auto & view_info = psc.view_info;
	view_info.foveation = foveation->get_parameters();
	view_info.display_time = cnx.get_offset().to_headset(info.predicted_display_time);
	if (view_info.alpha != do_alpha)
		pacer.reset();
	view_info.alpha = do_alpha;
	for (int eye = 0; eye < 2; ++eye)
	{
		const auto & frame_params = c->base.frame_params;
		view_info.fov[eye] = xrt_cast(frame_params.fovs[eye]);
		view_info.pose[eye] = xrt_cast(frame_params.poses[eye]);
		if (c->base.frame_params.one_projection_layer_fast_path)
		{
			const auto & proj = c->base.layer_accum.layers[0].data.proj;
			view_info.pose[eye] = xrt_cast(proj.v[eye].pose);
			view_info.fov[eye] = xrt_cast(proj.v[eye].fov);
		}
		else
		{
			xrt_relation_chain xrc{};
			xrt_space_relation result{};
			m_relation_chain_push_pose_if_not_identity(&xrc, &frame_params.poses[eye]);
			m_relation_chain_resolve(&xrc, &result);
			view_info.pose[eye] = xrt_cast(result.pose);
		}
	}
	// set bits to 1 for index 1..num encoder threads + 1
	psc.status = (1 << (encoder_threads.size() + 1)) - 2;
	psc.frame_index = info.frame_id;
	psc.status.notify_all();

	return VK_SUCCESS;
}

void wivrn_comp_target::flush()
{
#ifdef XRT_FEATURE_RENDERDOC
	if (auto r = renderdoc())
		r->StartFrameCapture(nullptr, nullptr);
#endif

	if (cnx.get_info().eye_gaze)
	{
		// FIXME: actually get the gaze data here
		auto now = os_monotonic_get_ns();
		cnx.add_tracking_request(device_id::EYE_GAZE, c->base.layer_accum.data.display_time_ns, now, now);
	}

	vk::CommandBuffer cmd;
	// apply foveation for current frame
	if (c->base.frame_params.one_projection_layer_fast_path)
	{
		const auto & data = c->base.layer_accum.layers[0].data;
		const auto & hmd = cnx.get_hmd().hmd;
		xrt_rect rect[] = {data.proj.v[0].sub.rect, data.proj.v[1].sub.rect};
		xrt_fov fov[] = {data.proj.v[0].fov, data.proj.v[1].fov};
		cmd = foveation->update_foveation_buffer(
		        c->nr.distortion.buffer,
		        data.flip_y,
		        rect,
		        fov);
	}
	else
	{
		const auto & hmd = cnx.get_hmd().hmd;
		xrt_rect rect[] = {
		        {.extent = {
		                 .w = int(hmd->views[0].display.w_pixels),
		                 .h = int(hmd->views[0].display.h_pixels),
		         }},
		        {.extent = {
		                 .w = int(hmd->views[1].display.w_pixels),
		                 .h = int(hmd->views[1].display.h_pixels),
		         }},
		};
		cmd = foveation->update_foveation_buffer(
		        c->nr.distortion.buffer,
		        false,
		        rect,
		        hmd->distortion.fov);
	}

	auto vk = get_vk();

	vk::Semaphore sem(semaphores.present_complete);

	vk::SubmitInfo submit_info{
	        .commandBufferCount = cmd ? 1u : 0,
	        .pCommandBuffers = &cmd,
	        .signalSemaphoreCount = 1,
	        .pSignalSemaphores = &sem,
	};

	{
		scoped_lock lock(vk->main_queue->mutex);
		wivrn_bundle->queue.submit(submit_info);
	}
}

void wivrn_comp_target::calc_frame_pacing(
        int64_t * out_frame_id,
        int64_t * out_wake_up_time_ns,
        int64_t * out_desired_present_time_ns,
        int64_t * out_present_slop_ns,
        int64_t * out_predicted_display_time_ns)
{
	pacer.predict(
	        *out_frame_id,
	        *out_wake_up_time_ns,
	        *out_desired_present_time_ns,
	        *out_present_slop_ns,
	        *out_predicted_display_time_ns);
}

void wivrn_comp_target::mark_timing_point(
        enum comp_target_timing_point point,
        int64_t frame_id,
        int64_t when_ns)
{
	pacer.mark_timing_point(point, frame_id, when_ns);

	switch (point)
	{
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			cnx.dump_time("wake_up", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_BEGIN:
			cnx.dump_time("begin", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			cnx.dump_time("submit", frame_id, when_ns);
			break;
		default:
			assert(false);
	}
}

VkResult wivrn_comp_target::update_timings()
{
	// TODO
	return VK_SUCCESS;
}

void wivrn_comp_target::set_title(const char * title)
{}

xrt_result_t wivrn_comp_target::get_refresh_rates(uint32_t * count, float * refresh_rates_hz)
{
	const auto & rates = cnx.get_info().available_refresh_rates;
	*count = std::min<uint32_t>(rates.size(), XRT_MAX_SUPPORTED_REFRESH_RATES);
	std::copy_n(rates.begin(), *count, refresh_rates_hz);
	return XRT_SUCCESS;
}

xrt_result_t wivrn_comp_target::get_current_refresh_rate(float * refresh_rate_hz)
{
	*refresh_rate_hz = desc.fps;
	return XRT_SUCCESS;
}

xrt_result_t wivrn_comp_target::request_refresh_rate(float refresh_rate_hz)
{
	requested_refresh_rate = refresh_rate_hz;
	if (refresh_rate_hz == 0.0f)
		refresh_rate_hz = get_default_rate(cnx.get_info(), *cnx.get_settings());

	try
	{
		cnx.send_control(to_headset::refresh_rate_change{.fps = refresh_rate_hz});
	}
	catch (...)
	{
		// ignore network errors
	}

	return XRT_SUCCESS;
}

wivrn_comp_target::~wivrn_comp_target()
{
	destroy_images();
}

void wivrn_comp_target::destroy()
{
	cnx.unset_comp_target();
	delete this;
}

void wivrn_comp_target::info_gpu(int64_t frame_id, int64_t gpu_start_ns, int64_t gpu_end_ns, int64_t when_ns)
{
	COMP_TRACE_MARKER();
}

void wivrn_comp_target::on_feedback(const from_headset::feedback & feedback, const clock_offset & o)
{
	uint8_t stream = feedback.stream_index;
	if (psc.status & 1)
		return;
	if (encoders.size() <= stream)
		return;
	encoders[stream]->on_feedback(feedback);
	if (not o)
		return;
	pacer.on_feedback(feedback, o);
}

void wivrn_comp_target::reset_encoders()
{
	pacer.reset();
	for (auto & encoder: encoders)
		encoder->reset();
	cnx.send_control(to_headset::video_stream_description{desc});
}

float wivrn_comp_target::get_requested_refresh_rate() const
{
	return requested_refresh_rate;
}
void wivrn_comp_target::reset_requested_refresh_rate()
{
	requested_refresh_rate = 0;
}

void wivrn_comp_target::pause()
{
	skip_encoding = true;
}

void wivrn_comp_target::resume()
{
	if (!skip_encoding)
		return;

	reset_encoders();
	skip_encoding = false;
}

void wivrn_comp_target::set_bitrate(uint32_t bitrate_bps)
{
	for (auto & encoder: encoders)
	{
		auto encoder_bps = (uint32_t)(bitrate_bps * encoder->bitrate_multiplier);
		U_LOG_D("Encoder %d bitrate: %d", encoder->stream_idx, encoder_bps);
		encoder->set_bitrate(encoder_bps);
	}
}

void wivrn_comp_target::set_refresh_rate(float refresh_rate_hz)
{
	U_LOG_I("Refresh rate change from %.0f to %.0f", desc.fps, refresh_rate_hz);
	desc.fps = refresh_rate_hz;
	c->frame_interval_ns = U_TIME_1S_IN_NS / refresh_rate_hz;
	pacer.set_frame_duration(c->frame_interval_ns);
	for (auto & encoder: encoders)
		encoder->set_framerate(refresh_rate_hz);
}

float wivrn_comp_target::get_refresh_rate()
{
	return desc.fps;
}

wivrn_comp_target::wivrn_comp_target(wivrn::wivrn_session & cnx, struct comp_compositor * c) :
        comp_target{
                .c = c,
                .init_pre_vulkan = method_pointer<&wivrn_comp_target::init_pre_vulkan>,
                .init_post_vulkan = method_pointer<&wivrn_comp_target::init_post_vulkan>,
                .check_ready = method_pointer<&wivrn_comp_target::check_ready>,
                .create_images = method_pointer<&wivrn_comp_target::create_images>,
                .has_images = method_pointer<&wivrn_comp_target::has_images>,
                .acquire = method_pointer<&wivrn_comp_target::acquire>,
                .present = method_pointer<&wivrn_comp_target::present>,
                .flush = method_pointer<&wivrn_comp_target::flush>,
                .calc_frame_pacing = method_pointer<&wivrn_comp_target::calc_frame_pacing>,
                .mark_timing_point = method_pointer<&wivrn_comp_target::mark_timing_point>,
                .update_timings = method_pointer<&wivrn_comp_target::update_timings>,
                .info_gpu = method_pointer<&wivrn_comp_target::info_gpu>,
                .set_title = method_pointer<&wivrn_comp_target::set_title>,
                .get_refresh_rates = method_pointer<&wivrn_comp_target::get_refresh_rates>,
                .get_current_refresh_rate = method_pointer<&wivrn_comp_target::get_current_refresh_rate>,
                .request_refresh_rate = method_pointer<&wivrn_comp_target::request_refresh_rate>,
                .destroy = method_pointer<&wivrn_comp_target::destroy>,
        },
        desc{.fps = get_default_rate(cnx.get_info(), *cnx.get_settings())},
        cnx(cnx),
        pacer(U_TIME_1S_IN_NS / desc.fps)
{
	c->frame_interval_ns = U_TIME_1S_IN_NS / desc.fps;
}
} // namespace wivrn
