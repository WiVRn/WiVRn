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

#include "wivrn_comp_target.h"

#include "driver/wivrn_session.h"
#include "encoder/video_encoder.h"
#include "utils/scoped_lock.h"
#include "wivrn_foveation.h"

#include "main/comp_compositor.h"
#include "math/m_space.h"
#include "xrt_cast.h"

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
#ifdef VK_KHR_video_encode_h264
        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
#endif
#ifdef VK_KHR_video_encode_h265
        VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME,
#endif
};

static void target_init_semaphores(struct wivrn_comp_target * cn);

static void target_fini_semaphores(struct wivrn_comp_target * cn);

static inline struct vk_bundle * get_vk(struct wivrn_comp_target * cn)
{
	return &cn->c->base.vk;
}

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

static void destroy_images(struct wivrn_comp_target * cn)
{
	if (cn->images == nullptr)
		return;

	cn->psc.status = 1;
	cn->psc.status.notify_all();
	cn->encoder_threads.clear();
	cn->encoders.clear();

	cn->psc.images.clear();

	free(cn->images);
	cn->images = NULL;

	target_fini_semaphores(cn);
}

static void comp_wivrn_present_thread(std::stop_token stop_token, wivrn_comp_target * cn, int index, std::vector<std::shared_ptr<video_encoder>> encoders);

static void create_encoders(wivrn_comp_target * cn)
{
	auto vk = get_vk(cn);
	assert(cn->encoders.empty());
	assert(cn->encoder_threads.empty());
	assert(cn->wivrn_bundle);
	cn->psc.status = 0;

	to_headset::video_stream_description & desc = cn->desc;
	desc.width = cn->width;
	desc.height = cn->height;
	desc.foveation = cn->cnx.set_foveated_size(desc.width, desc.height);

	std::map<int, std::vector<std::shared_ptr<video_encoder>>> thread_params;

	for (auto & settings: cn->settings)
	{
		uint8_t stream_index = cn->encoders.size();
		auto & encoder = cn->encoders.emplace_back(
		        video_encoder::create(*cn->wivrn_bundle, settings, stream_index, desc.width, desc.height, desc.fps));
		desc.items.push_back(settings);

		thread_params[settings.group].emplace_back(encoder);
	}

	for (auto & [group, params]: thread_params)
	{
		auto & thread = cn->encoder_threads.emplace_back(
		        comp_wivrn_present_thread, cn, cn->encoder_threads.size(), std::move(params));
		std::string name = "encoder " + std::to_string(group);
		pthread_setname_np(thread.native_handle(), name.c_str());
	}
	cn->cnx.send_control(to_headset::video_stream_description{desc});
}

static VkResult create_images(struct wivrn_comp_target * cn, vk::ImageUsageFlags flags)
{
	auto vk = get_vk(cn);
	assert(cn->image_count > 0);
	COMP_DEBUG(cn->c, "Creating %d images.", cn->image_count);
	auto & device = cn->wivrn_bundle->device;

	destroy_images(cn);

	auto format = vk::Format(cn->format);

	cn->images = U_TYPED_ARRAY_CALLOC(struct comp_target_image, cn->image_count);

#if WIVRN_USE_VULKAN_ENCODE
	auto [video_profiles, encoder_flags] = video_encoder::get_create_image_info(cn->settings);

	vk::VideoProfileListInfoKHR video_profile_list{
	        .profileCount = uint32_t(video_profiles.size()),
	        .pProfiles = video_profiles.data(),
	};
#endif

	cn->psc.images.resize(cn->image_count);
	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		std::array formats = {
		        vk::Format::eR8Unorm,
		        vk::Format::eR8G8Unorm,
		        format,
		};
		vk::ImageFormatListCreateInfo formats_info{
		        .viewFormatCount = formats.size(),
		        .pViewFormats = formats.data(),
		};

#if WIVRN_USE_VULKAN_ENCODE
		if (video_profile_list.profileCount)
			formats_info.pNext = &video_profile_list;
#endif

		auto & image = cn->psc.images[i].image;
		image = image_allocation(
		        device, {
		                        .pNext = &formats_info,
		                        .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
		                        .imageType = vk::ImageType::e2D,
		                        .format = format,
		                        .extent = {
		                                .width = cn->width,
		                                .height = cn->height,
		                                .depth = 1,
		                        },
		                        .mipLevels = 1,
		                        .arrayLayers = 2, // colour then alpha
		                        .samples = vk::SampleCountFlagBits::e1,
		                        .tiling = vk::ImageTiling::eOptimal,
		                        .usage = flags
#if WIVRN_USE_VULKAN_ENCODE
		                                 | encoder_flags
#endif
		                                 | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
		                        .sharingMode = vk::SharingMode::eExclusive,
		                },
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        });
		cn->images[i].handle = image;
	}

	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		auto & item = cn->psc.images[i];
		vk::ImageViewUsageCreateInfo usage{
		        .usage = flags,
		};
		item.image_view_y = vk::raii::ImageView(device,
		                                        {
		                                                .pNext = &usage,
		                                                .image = item.image,
		                                                .viewType = vk::ImageViewType::e2DArray,
		                                                .format = vk::Format::eR8Unorm,
		                                                .subresourceRange = {
		                                                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                                                        .levelCount = 1,
		                                                        .layerCount = 2,
		                                                },
		                                        });
		item.image_view_cbcr = vk::raii::ImageView(device,
		                                           {
		                                                   .pNext = &usage,
		                                                   .image = item.image,
		                                                   .viewType = vk::ImageViewType::e2DArray,
		                                                   .format = vk::Format::eR8G8Unorm,
		                                                   .subresourceRange = {
		                                                           .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                                                           .levelCount = 1,
		                                                           .layerCount = 2,
		                                                   },
		                                           });
		cn->images[i].view = VkImageView(*item.image_view_y);
		cn->images[i].view_cbcr = VkImageView(*item.image_view_cbcr);
	}

	cn->psc.fence = vk::raii::Fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});

	cn->psc.command_buffer = std::move(device.allocateCommandBuffers(
	        {.commandPool = *cn->command_pool,
	         .commandBufferCount = 1})[0]);

	return VK_SUCCESS;
}

static bool comp_wivrn_init_pre_vulkan(struct comp_target * ct)
{
	return true;
}

static bool comp_wivrn_init_post_vulkan(struct comp_target * ct, uint32_t preferred_width, uint32_t preferred_height)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	auto vk = get_vk(cn);
	try
	{
		cn->wivrn_bundle.emplace(*vk,
		                         wivrn_comp_target::wanted_instance_extensions,
		                         wivrn_comp_target::wanted_device_extensions);
		cn->command_pool = vk::raii::CommandPool(
		        cn->wivrn_bundle->device,
		        {
		                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		                .queueFamilyIndex = vk->queue_family_index,
		        });
	}
	catch (std::exception & e)
	{
		U_LOG_E("Compositor target init failed: %s", e.what());
		return false;
	}

	try
	{
		cn->settings = get_encoder_settings(
		        *cn->wivrn_bundle,
		        cn->c->settings.preferred.width,
		        cn->c->settings.preferred.height,
		        cn->cnx.get_info());
		print_encoders(cn->settings);
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to create video encoder: %s", e.what());
		return false;
	}

	if (cn->cnx.has_dynamic_foveation())
	{
		cn->foveation_renderer = std::make_unique<wivrn_foveation_renderer>(*cn->wivrn_bundle, cn->command_pool);
	}

	return true;
}

static bool comp_wivrn_check_ready(struct comp_target * ct)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	if (not cn->cnx.connected())
		return false;

	// This function is called before on each frame before reprojection
	// hijack it so that we can dynamically change ATW
	cn->c->debug.atw_off = true;
	for (int eye = 0; eye < 2; ++eye)
	{
		const auto & layer_accum = cn->c->base.layer_accum;
		if (layer_accum.layer_count > 1 or
		    layer_accum.layers[0].data.type != XRT_LAYER_PROJECTION)
		{
			// We are not in the trivial single stereo projection layer
			// reprojection must be done
			cn->c->debug.atw_off = false;
		}
	}
	return true;
}

static void target_fini_semaphores(struct wivrn_comp_target * cn)
{
	struct vk_bundle * vk = get_vk(cn);

	if (cn->semaphores.present_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, cn->semaphores.present_complete, NULL);
		cn->semaphores.present_complete = VK_NULL_HANDLE;
	}

	if (cn->semaphores.render_complete != VK_NULL_HANDLE)
	{
		vk->vkDestroySemaphore(vk->device, cn->semaphores.render_complete, NULL);
		cn->semaphores.render_complete = VK_NULL_HANDLE;
	}
}

static void target_init_semaphores(struct wivrn_comp_target * cn)
{
	struct vk_bundle * vk = get_vk(cn);
	VkResult ret;

	target_fini_semaphores(cn);

	VkSemaphoreCreateInfo info = {
	        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cn->semaphores.present_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(cn->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	cn->semaphores.render_complete_is_timeline = false;
	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &cn->semaphores.render_complete);
	if (ret != VK_SUCCESS)
	{
		COMP_ERROR(cn->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}
}

static void comp_wivrn_create_images(struct comp_target * ct, const struct comp_target_create_images_info * create_info)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	// Free old images.
	destroy_images(cn);

	target_init_semaphores(cn);

	// FIXME: select preferred format
	ct->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	ct->width = create_info->extent.width;
	ct->height = create_info->extent.height;
	ct->surface_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

	cn->image_count = 3;
	cn->color_space = create_info->color_space;

	VkResult res = create_images(cn, vk::ImageUsageFlags(create_info->image_usage));
	if (res != VK_SUCCESS)
	{
		vk_print_result(get_vk(cn), __FILE__, __LINE__, __func__, res, "create_images");
		// TODO
		abort();
	}
	create_encoders(cn);
}

static bool comp_wivrn_has_images(struct comp_target * ct)
{
	return ct->images;
}

static VkResult comp_wivrn_acquire(struct comp_target * ct, uint32_t * out_index)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	while (true)
	{
		for (uint32_t i = 0; i < ct->image_count; i++)
		{
			if (cn->psc.images[i].status == pseudo_swapchain::status_t::free)
			{
				cn->psc.images[i].status = pseudo_swapchain::status_t::acquired;
				*out_index = i;
				return VK_SUCCESS;
			}
		}
	};
}

static void comp_wivrn_present_thread(std::stop_token stop_token, wivrn_comp_target * cn, int index, std::vector<std::shared_ptr<video_encoder>> encoders)
{
	auto & vk = *cn->wivrn_bundle;
	U_LOG_I("Starting encoder thread %d", index);

	const uint8_t status_bit = 1 << (index + 1);

	while (not stop_token.stop_requested())
	{
		{
			auto status = cn->psc.status.load();
			// Bit 0 to request exit
			if (status & 1)
				return;

			if (not(status & status_bit))
			{
				cn->psc.status.wait(status);
				continue;
			}
		}

		// Get local copies before releasing the image
		auto view_info = cn->psc.view_info;
		auto frame_index = cn->psc.frame_index;

		auto res = vk.device.waitForFences(*cn->psc.fence, true, UINT64_MAX);

		// Update encoder status, release image
		if ((cn->psc.status &= ~status_bit) == 0)
		{
			cn->psc.status.notify_all();
			for (auto & img: cn->psc.images)
			{
				if (img.status == pseudo_swapchain::status_t::encoding)
				{
					img.status = pseudo_swapchain::status_t::free;
					break;
				}
			}
		}

		try
		{
			for (auto & encoder: encoders)
			{
				if (encoder->channels == to_headset::video_stream_description::channels_t::colour or view_info.alpha)
					encoder->encode(cn->cnx, view_info, frame_index);
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
	}
}

static VkResult comp_wivrn_present(struct comp_target * ct,
                                   VkQueue queue_,
                                   uint32_t index,
                                   uint64_t timeline_semaphore_value,
                                   int64_t desired_present_time_ns,
                                   int64_t present_slop_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	assert(index < cn->image_count);
	assert(cn->psc.images[index].status == pseudo_swapchain::status_t::acquired);

	struct vk_bundle * vk = get_vk(cn);
	auto res = cn->wivrn_bundle->device.waitForFences(*cn->psc.fence, true, UINT64_MAX);

	vk::Semaphore wait_semaphore(cn->semaphores.render_complete);
	vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTransfer;
	vk::SubmitInfo submit_info{
	        .waitSemaphoreCount = 1,
	        .pWaitSemaphores = &wait_semaphore,
	        .pWaitDstStageMask = &wait_stage,
	};

	if (cn->c->base.layer_accum.layer_count == 0 or not cn->cnx.get_offset())
	{
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info);
		cn->psc.images[index].status = pseudo_swapchain::status_t::free;
		return VK_SUCCESS;
	}

	auto & command_buffer = cn->psc.command_buffer;
	auto & psc_image = cn->psc.images[index];

	// Wait for encoders to be done with previous frame
	for (auto status = cn->psc.status.load(); status != 0; status = cn->psc.status)
		cn->psc.status.wait(status);

	command_buffer.reset();
	command_buffer.begin(vk::CommandBufferBeginInfo{
	        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	});

	cn->wivrn_bundle->device.resetFences(*cn->psc.fence);
	psc_image.status = pseudo_swapchain::status_t::encoding;
	auto info = cn->pacer.present_to_info(desired_present_time_ns);
	const bool do_alpha = cn->c->base.layer_accum.data.env_blend_mode == XRT_BLEND_MODE_ALPHA_BLEND;

	bool need_queue_transfer = false;
	std::vector<vk::Semaphore> present_done_sem;
	for (auto & encoder: cn->encoders)
	{
		if (encoder->channels == to_headset::video_stream_description::channels_t::alpha and not do_alpha)
			continue;
		auto [transfer, sem] = encoder->present_image(psc_image.image, command_buffer, info.frame_id);
		need_queue_transfer |= transfer;
		if (sem)
			present_done_sem.push_back(sem);
	}

#if WIVRN_USE_VULKAN_ENCODE
	if (need_queue_transfer)
	{
		vk::ImageMemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eMemoryRead,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
		        .newLayout = vk::ImageLayout::eVideoEncodeSrcKHR,
		        .srcQueueFamilyIndex = vk->queue_family_index,
		        .dstQueueFamilyIndex = vk->encode_queue_family_index,
		        .image = psc_image.image,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = 2},
		};
		command_buffer.pipelineBarrier(
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::PipelineStageFlagBits::eNone,
		        {},
		        {},
		        {},
		        barrier);
	}
	submit_info.setSignalSemaphores(present_done_sem);
#endif
	command_buffer.end();
	submit_info.setCommandBuffers(*command_buffer);

	{
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info, *cn->psc.fence);
		for (auto & encoder: cn->encoders)
		{
			if (encoder->channels == to_headset::video_stream_description::channels_t::alpha and not do_alpha)
				continue;
			encoder->post_submit();
		}
	}

#ifdef XRT_FEATURE_RENDERDOC
	if (auto r = renderdoc())
		r->EndFrameCapture(NULL, NULL);
#endif

	auto & view_info = cn->psc.view_info;
	view_info.foveation = cn->cnx.get_foveation_parameters();
	view_info.display_time = cn->cnx.get_offset().to_headset(info.predicted_display_time);
	if (view_info.alpha != do_alpha)
		cn->pacer.reset();
	view_info.alpha = do_alpha;
	for (int eye = 0; eye < 2; ++eye)
	{
		const auto & frame_params = cn->c->base.frame_params;
		view_info.fov[eye] = xrt_cast(frame_params.fovs[eye]);
		view_info.pose[eye] = xrt_cast(frame_params.poses[eye]);
		if (cn->c->debug.atw_off)
		{
			const auto & proj = cn->c->base.layer_accum.layers[0].data.proj;
			view_info.pose[eye] = xrt_cast(proj.v[eye].pose);
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
	cn->psc.status = (1 << (cn->encoder_threads.size() + 1)) - 2;
	cn->psc.frame_index = info.frame_id;
	cn->psc.status.notify_all();

	return VK_SUCCESS;
}

static void comp_wivrn_flush(struct comp_target * ct)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

#ifdef XRT_FEATURE_RENDERDOC
	if (auto r = renderdoc())
		r->StartFrameCapture(NULL, NULL);
#endif

	// apply foveation for current frame
	if (cn->cnx.apply_dynamic_foveation())
		// foveation renderer already signaled the semaphore; nothing to do
		return;

	struct vk_bundle * vk = get_vk(cn);

	vk::Semaphore sem(cn->semaphores.present_complete);

	vk::SubmitInfo submit_info{
	        .signalSemaphoreCount = 1,
	        .pSignalSemaphores = &sem,
	};

	{
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info);
	}
}

static void comp_wivrn_calc_frame_pacing(struct comp_target * ct,
                                         int64_t * out_frame_id,
                                         int64_t * out_wake_up_time_ns,
                                         int64_t * out_desired_present_time_ns,
                                         int64_t * out_present_slop_ns,
                                         int64_t * out_predicted_display_time_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	cn->pacer.predict(
	        *out_frame_id,
	        *out_wake_up_time_ns,
	        *out_desired_present_time_ns,
	        *out_present_slop_ns,
	        *out_predicted_display_time_ns);
}

static void comp_wivrn_mark_timing_point(struct comp_target * ct,
                                         enum comp_target_timing_point point,
                                         int64_t frame_id,
                                         int64_t when_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	cn->pacer.mark_timing_point(point, frame_id, when_ns);

	switch (point)
	{
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			cn->cnx.dump_time("wake_up", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_BEGIN:
			cn->cnx.dump_time("begin", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			cn->cnx.dump_time("submit", frame_id, when_ns);
			break;
		default:
			assert(false);
	}
}

static VkResult comp_wivrn_update_timings(struct comp_target * ct)
{
	// TODO
	return VK_SUCCESS;
}

static void comp_wivrn_set_title(struct comp_target * ct, const char * title)
{}

static xrt_result_t comp_wivrn_get_refresh_rates(struct comp_target * ct, uint32_t * count, float * refresh_rates_hz)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	const auto & rates = cn->cnx.get_info().available_refresh_rates;
	*count = std::min<uint32_t>(rates.size(), XRT_MAX_SUPPORTED_REFRESH_RATES);
	std::copy_n(rates.begin(), *count, refresh_rates_hz);
	return XRT_SUCCESS;
}

static xrt_result_t comp_wivrn_get_refresh_rate(struct comp_target * ct, float * refresh_rate_hz)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	*refresh_rate_hz = cn->desc.fps;
	return XRT_SUCCESS;
}

static xrt_result_t comp_wivrn_request_refresh_rate(struct comp_target * ct, float refresh_rate_hz)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	if (refresh_rate_hz == 0.0f)
		refresh_rate_hz = cn->cnx.get_info().preferred_refresh_rate;

	cn->desc.fps = refresh_rate_hz;
	cn->pacer.set_frame_duration(U_TIME_1S_IN_NS / refresh_rate_hz);
	cn->cnx.send_control(to_headset::video_stream_description{cn->desc});
	return XRT_SUCCESS;
}

wivrn_comp_target::~wivrn_comp_target()
{
	if (wivrn_bundle)
		wivrn_bundle->device.waitIdle();
	destroy_images(this);
}

static void comp_wivrn_destroy(struct comp_target * ct)
{
	delete (struct wivrn_comp_target *)ct;
}

static void comp_wivrn_info_gpu(struct comp_target * ct, int64_t frame_id, int64_t gpu_start_ns, int64_t gpu_end_ns, int64_t when_ns)
{
	COMP_TRACE_MARKER();
}

void wivrn_comp_target::on_feedback(const from_headset::feedback & feedback, const clock_offset & o)
{
	if (not o)
		return;
	uint8_t stream = feedback.stream_index;
	if (psc.status & 1)
		return;
	if (encoders.size() <= stream)
		return;
	encoders[stream]->on_feedback(feedback);
	pacer.on_feedback(feedback, o);
}

void wivrn_comp_target::reset_encoders()
{
	pacer.reset();
	for (auto & encoder: encoders)
		encoder->reset();
	cnx.send_control(to_headset::video_stream_description{desc});
}

void wivrn_comp_target::render_dynamic_foveation(std::array<to_headset::foveation_parameter, 2> foveation)
{
	assert(foveation_renderer);

	vk::Semaphore sem(semaphores.present_complete);

	vk::SubmitInfo submit_info{
	        .commandBufferCount = 1,
	        .pCommandBuffers = &*foveation_renderer->cmd_buf,
	        .signalSemaphoreCount = 1,
	        .pSignalSemaphores = &sem,
	};

	foveation_renderer->render_distortion_images(foveation, c->nr.distortion.images, c->nr.distortion.image_views);

	{
		scoped_lock lock(c->base.vk.queue_mutex);
		wivrn_bundle->queue.submit(submit_info);
	}
}

wivrn_comp_target::wivrn_comp_target(wivrn::wivrn_session & cnx, struct comp_compositor * c) :
        comp_target{
                .c = c,
                .init_pre_vulkan = comp_wivrn_init_pre_vulkan,
                .init_post_vulkan = comp_wivrn_init_post_vulkan,
                .check_ready = comp_wivrn_check_ready,
                .create_images = comp_wivrn_create_images,
                .has_images = comp_wivrn_has_images,
                .acquire = comp_wivrn_acquire,
                .present = comp_wivrn_present,
                .flush = comp_wivrn_flush,
                .calc_frame_pacing = comp_wivrn_calc_frame_pacing,
                .mark_timing_point = comp_wivrn_mark_timing_point,
                .update_timings = comp_wivrn_update_timings,
                .info_gpu = comp_wivrn_info_gpu,
                .set_title = comp_wivrn_set_title,
                .get_refresh_rates = comp_wivrn_get_refresh_rates,
                .get_refresh_rate = comp_wivrn_get_refresh_rate,
                .request_refresh_rate = comp_wivrn_request_refresh_rate,
                .destroy = comp_wivrn_destroy,
        },
        desc{.fps = cnx.get_info().preferred_refresh_rate},
        pacer(U_TIME_1S_IN_NS / desc.fps),
        cnx(cnx)
{
}
} // namespace wivrn
