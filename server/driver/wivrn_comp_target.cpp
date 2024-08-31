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
#include "encoder/video_encoder.h"
#include "main/comp_compositor.h"
#include "math/m_space.h"
#include "utils/scoped_lock.h"
#include "wivrn_foveation.h"
#include "xrt_cast.h"
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>

using namespace xrt::drivers::wivrn;

std::vector<const char *> wivrn_comp_target::wanted_instance_extensions = {};
std::vector<const char *> wivrn_comp_target::wanted_device_extensions = {
// For FFMPEG
#ifdef VK_EXT_external_memory_dma_buf
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
#endif
#ifdef VK_EXT_image_drm_format_modifier
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
#endif
};

static void target_init_semaphores(struct wivrn_comp_target * cn);

static void target_fini_semaphores(struct wivrn_comp_target * cn);

static inline struct vk_bundle * get_vk(struct wivrn_comp_target * cn)
{
	return &cn->c->base.vk;
}

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

static void comp_wivrn_present_thread(std::stop_token stop_token, wivrn_comp_target * cn, int index, std::vector<std::shared_ptr<VideoEncoder>> encoders);

static void create_encoders(wivrn_comp_target * cn)
{
	auto vk = get_vk(cn);
	assert(cn->encoders.empty());
	assert(cn->encoder_threads.empty());
	assert(cn->wivrn_bundle);
	cn->psc.status = 0;

	auto & desc = cn->desc;
	desc.width = cn->width;
	desc.height = cn->height;
	desc.foveation = cn->cnx->set_foveated_size(desc.width, desc.height);

	std::map<int, std::vector<std::shared_ptr<VideoEncoder>>> thread_params;

	for (auto & settings: cn->settings)
	{
		uint8_t stream_index = cn->encoders.size();
		auto & encoder = cn->encoders.emplace_back(
		        VideoEncoder::Create(*cn->wivrn_bundle, settings, stream_index, desc.width, desc.height, desc.fps));
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
	cn->pacer.set_stream_count(cn->encoders.size());
	cn->cnx->send_control(desc);
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

	cn->psc.images.resize(cn->image_count);
	std::vector<vk::Image> rgb;
	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		auto & image = cn->psc.images[i].image;
		image = image_allocation(
		        device, {
		                        .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
		                        .imageType = vk::ImageType::e2D,
		                        .format = format,
		                        .extent = {
		                                .width = cn->width,
		                                .height = cn->height,
		                                .depth = 1,
		                        },
		                        .mipLevels = 1,
		                        .arrayLayers = 1,
		                        .samples = vk::SampleCountFlagBits::e1,
		                        .tiling = vk::ImageTiling::eOptimal,
		                        .usage = flags | vk::ImageUsageFlagBits::eStorage,
		                        .sharingMode = vk::SharingMode::eExclusive,
		                },
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        });
		cn->images[i].handle = image;
		rgb.push_back(image);
	}

	for (uint32_t i = 0; i < cn->image_count; i++)
	{
		auto & item = cn->psc.images[i];
		vk::ImageViewUsageCreateInfo usage{
		        .usage = flags,
		};
		item.image_view = vk::raii::ImageView(device,
		                                      {
		                                              .pNext = &usage,
		                                              .image = item.image,
		                                              .viewType = vk::ImageViewType::e2D,
		                                              .format = format,
		                                              .subresourceRange = {
		                                                      .aspectMask = vk::ImageAspectFlagBits::eColor,
		                                                      .levelCount = 1,
		                                                      .layerCount = 1,
		                                              },
		                                      });
		cn->images[i].view = *item.image_view;
	}

	cn->psc.yuv = yuv_converter(vk->physical_device, device, rgb, format, vk::Extent2D{cn->width, cn->height});

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
		        cn->cnx->get_info().supported_codecs);
		print_encoders(cn->settings);
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to create video encoder: %s", e.what());
		return false;
	}

	if (cn->cnx->has_dynamic_foveation())
	{
		cn->foveation_renderer = std::make_unique<wivrn_foveation_renderer>(*cn->wivrn_bundle, cn->command_pool);
	}

	return true;
}

static bool comp_wivrn_check_ready(struct comp_target * ct)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	if (not cn->cnx->connected())
		return false;

	// This function is called before on each frame before reprojection
	// hijack it so that we can dynamically change ATW
	cn->c->debug.atw_off = true;
	for (int eye = 0; eye < 2; ++eye)
	{
		const auto & slot = cn->c->base.slot;
		if (slot.layer_count > 1 or
		    slot.layers[0].data.type != XRT_LAYER_PROJECTION)
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

	uint64_t now_ns = os_monotonic_get_ns();

	// Free old images.
	destroy_images(cn);

	target_init_semaphores(cn);

	// FIXME: select preferred format
	assert(create_info->format_count > 0);
	ct->format = create_info->formats[0];
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

static void comp_wivrn_present_thread(std::stop_token stop_token, wivrn_comp_target * cn, int index, std::vector<std::shared_ptr<VideoEncoder>> encoders)
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

		auto res = vk.device.waitForFences(*cn->psc.fence, true, UINT64_MAX);

		try
		{
			for (auto & encoder: encoders)
			{
				encoder->Encode(*cn->cnx, cn->psc.view_info, cn->psc.frame_index);
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
		if ((cn->psc.status &= ~status_bit) == 0)
		{
			for (auto & img: cn->psc.images)
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

static VkResult comp_wivrn_present(struct comp_target * ct,
                                   VkQueue queue_,
                                   uint32_t index,
                                   uint64_t timeline_semaphore_value,
                                   uint64_t desired_present_time_ns,
                                   uint64_t present_slop_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	assert(index < cn->image_count);
	assert(cn->psc.images[index].status == pseudo_swapchain::status_t::acquired);

	struct vk_bundle * vk = get_vk(cn);

	auto & command_buffer = cn->psc.command_buffer;
	command_buffer.reset();
	command_buffer.begin(vk::CommandBufferBeginInfo{});

	vk::Semaphore wait_semaphore = cn->semaphores.render_complete;
	vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eComputeShader;
	vk::SubmitInfo submit_info{
	        .waitSemaphoreCount = 1,
	        .pWaitSemaphores = &wait_semaphore,
	        .pWaitDstStageMask = &wait_stage,
	        .commandBufferCount = 1,
	        .pCommandBuffers = &*command_buffer,
	};

	bool skip = cn->c->base.slot.layer_count == 0 or not cn->cnx->get_offset();
	if (cn->psc.status != 0)
	{
		U_LOG_W("Encoders not done with previous image, skip one");
		skip = true;
	}

	if (skip)
	{
		command_buffer.end();
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info);
		cn->psc.images[index].status = pseudo_swapchain::status_t::free;
		return VK_SUCCESS;
	}

	cn->psc.images[index].status = pseudo_swapchain::status_t::encoding;

	auto & yuv = cn->psc.yuv;
	yuv.record_draw_commands(command_buffer, index);
	for (auto & encoder: cn->encoders)
	{
		encoder->PresentImage(yuv, command_buffer);
	}
	command_buffer.end();

	auto res = cn->wivrn_bundle->device.waitForFences(*cn->psc.fence, true, UINT64_MAX);

	cn->wivrn_bundle->device.resetFences(*cn->psc.fence);
	{
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info, *cn->psc.fence);
	}

	auto & view_info = cn->psc.view_info;
	view_info.foveation = cn->cnx->get_foveation_parameters();
	auto info = cn->pacer.present_to_info(desired_present_time_ns);
	view_info.display_time = cn->cnx->get_offset().to_headset(info.predicted_display_time);
	for (int eye = 0; eye < 2; ++eye)
	{
		const auto & slot = cn->c->base.slot;
		view_info.fov[eye] = xrt_cast(slot.fovs[eye]);
		view_info.pose[eye] = xrt_cast(slot.poses[eye]);
		if (cn->c->debug.atw_off)
		{
			const auto & proj = slot.layers[0].data.proj;
			view_info.pose[eye] = xrt_cast(proj.v[eye].pose);
		}
		else
		{
			xrt_relation_chain xrc{};
			xrt_space_relation result{};
			m_relation_chain_push_pose_if_not_identity(&xrc, &slot.poses[eye]);
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

	// apply foveation for current frame
	if (cn->cnx->apply_dynamic_foveation())
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
                                         uint64_t * out_wake_up_time_ns,
                                         uint64_t * out_desired_present_time_ns,
                                         uint64_t * out_present_slop_ns,
                                         uint64_t * out_predicted_display_time_ns)
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
                                         uint64_t when_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	cn->pacer.mark_timing_point(point, frame_id, when_ns);

	switch (point)
	{
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			cn->cnx->dump_time("wake_up", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_BEGIN:
			cn->cnx->dump_time("begin", frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			cn->cnx->dump_time("submit", frame_id, when_ns);
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

static void comp_wivrn_info_gpu(struct comp_target * ct, int64_t frame_id, uint64_t gpu_start_ns, uint64_t gpu_end_ns, uint64_t when_ns)
{
	COMP_TRACE_MARKER();
}

void wivrn_comp_target::on_feedback(const from_headset::feedback & feedback, const clock_offset & o)
{
	if (not o)
		return;
	pacer.on_feedback(feedback, o);
	if (not feedback.sent_to_decoder)
	{
		if (encoders.size() < feedback.stream_index)
			return;
		encoders[feedback.stream_index]->SyncNeeded();
	}
}

void wivrn_comp_target::reset_encoders()
{
	pacer.reset();
	for (auto & encoder: encoders)
	{
		encoder->SyncNeeded();
	}
	cnx->send_control(desc);
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

wivrn_comp_target::wivrn_comp_target(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, struct comp_compositor * c, float fps) :
        comp_target{},
        pacer(U_TIME_1S_IN_NS / fps),
        cnx(cnx)
{
	check_ready = comp_wivrn_check_ready;
	create_images = comp_wivrn_create_images;
	has_images = comp_wivrn_has_images;
	acquire = comp_wivrn_acquire;
	present = comp_wivrn_present;
	calc_frame_pacing = comp_wivrn_calc_frame_pacing;
	mark_timing_point = comp_wivrn_mark_timing_point;
	update_timings = comp_wivrn_update_timings;
	info_gpu = comp_wivrn_info_gpu;
	destroy = comp_wivrn_destroy;
	init_pre_vulkan = comp_wivrn_init_pre_vulkan;
	init_post_vulkan = comp_wivrn_init_post_vulkan;
	set_title = comp_wivrn_set_title;
	flush = comp_wivrn_flush;
	this->fps = fps;
	desc.fps = fps;
	this->c = c;
}
