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
#include "util/u_pacing.h"
#include "xrt_cast.h"
#include <condition_variable>
#include <list>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

using namespace xrt::drivers::wivrn;

static const uint8_t image_free = 0;
static const uint8_t image_acquired = 1;

std::vector<const char *> wivrn_comp_target::wanted_instance_extensions = {};
std::vector<const char *> wivrn_comp_target::wanted_device_extensions = {
// For FFMPEG
#ifdef VK_KHR_external_memory_fd
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
#endif
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

static void destroy_images(struct wivrn_comp_target * cn)
{
	if (cn->images == NULL)
	{
		return;
	}

	cn->encoder_threads.clear();
	cn->encoders.clear();

	cn->psc.images.clear();

	free(cn->images);
	cn->images = NULL;
	cn->psc.images.clear();

	target_fini_semaphores(cn);
}

namespace
{
class scoped_lock
{
	os_mutex & mutex;

public:
	scoped_lock(os_mutex & mutex) :
	        mutex(mutex)
	{
		os_mutex_lock(&mutex);
	}
	~scoped_lock()
	{
		os_mutex_unlock(&mutex);
	}
};
}; // namespace

struct encoder_thread_param
{
	wivrn_comp_target * cn;
	wivrn_comp_target::encoder_thread * thread;
	std::vector<std::shared_ptr<VideoEncoder>> encoders;
};

static void * comp_wivrn_present_thread(void * void_param);

static void create_encoders(wivrn_comp_target * cn, std::vector<encoder_settings> & _settings)
{
	auto vk = get_vk(cn);
	assert(cn->encoders.empty());
	assert(cn->encoder_threads.empty());
	assert(cn->wivrn_bundle);

	to_headset::video_stream_description desc{};
	desc.width = cn->width;
	desc.height = cn->height;
	desc.fps = cn->fps;
	desc.foveation = cn->cnx->get_foveation_parameters();

	std::map<int, encoder_thread_param> thread_params;

	for (auto & settings: _settings)
	{
		uint8_t stream_index = cn->encoders.size();
		auto & encoder = cn->encoders.emplace_back(
		        VideoEncoder::Create(*cn->wivrn_bundle, settings, stream_index, desc.width, desc.height, desc.fps));
		desc.items.push_back(settings);

		thread_params[settings.group].encoders.emplace_back(encoder);
	}

	for (auto & [group, params]: thread_params)
	{
		auto params_ptr = new encoder_thread_param(params);
		auto & thread = cn->encoder_threads.emplace_back();
		thread.index = cn->encoder_threads.size() - 1;
		params_ptr->thread = &thread;
		params_ptr->cn = cn;
		os_thread_helper_start(&thread.thread, comp_wivrn_present_thread, params_ptr);
		std::string name = "encoder " + std::to_string(group);
		os_thread_helper_name(&thread.thread, name.c_str());
	}
	cn->cnx->send_control(desc);
}

static VkResult create_images(struct wivrn_comp_target * cn, vk::ImageUsageFlags flags, const std::vector<xrt::drivers::wivrn::encoder_settings> & settings)
{
	auto vk = get_vk(cn);
	assert(cn->image_count > 0);
	COMP_DEBUG(cn->c, "Creating %d images.", cn->image_count);
	auto & device = cn->wivrn_bundle->device;

	destroy_images(cn);

	auto format = vk::Format(cn->format);

	cn->images = U_TYPED_ARRAY_CALLOC(struct comp_target_image, cn->image_count);

	cn->psc.images.resize(cn->image_count);
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
		item.yuv = yuv_converter(vk->physical_device, device, item.image, format, vk::Extent2D{cn->width, cn->height});

		item.fence = vk::raii::Fence(device, vk::FenceCreateInfo());

		item.command_buffer = std::move(device.allocateCommandBuffers(
		        {.commandPool = *cn->command_pool,
		         .commandBufferCount = 1})[0]);
	}

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
		return true;
	}
	catch (std::exception & e)
	{
		U_LOG_E("Compositor target init failed: %s", e.what());
	}
	return false;
}

static bool comp_wivrn_check_ready(struct comp_target * ct)
{
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
	if (cn->upc == NULL)
	{
		u_pc_fake_create(ct->c->settings.nominal_frame_interval_ns, now_ns, &cn->upc);
	}

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
	cn->format = ct->format;
	cn->width = create_info->extent.width;
	cn->height = create_info->extent.height;
	cn->color_space = create_info->color_space;

	auto settings = get_encoder_settings(*cn->wivrn_bundle->physical_device, cn->width, cn->height);

	VkResult res = create_images(cn, vk::ImageUsageFlags(create_info->image_usage), settings);
	if (res != VK_SUCCESS)
	{
		vk_print_result(get_vk(cn), __FILE__, __LINE__, __func__, res, "create_images");
		// TODO
		abort();
	}

	try
	{
		create_encoders(cn, settings);
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to create video encoder: %s", e.what());
		abort();
	}
}

static bool comp_wivrn_has_images(struct comp_target * ct)
{
	return ct->images;
}

static VkResult comp_wivrn_acquire(struct comp_target * ct, uint32_t * out_index)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
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

	while (true)
	{
		std::lock_guard lock(cn->psc.mutex);
		for (uint32_t i = 0; i < ct->image_count; i++)
		{
			if (cn->psc.images[i].status == image_free)
			{
				cn->psc.images[i].status = image_acquired;

				*out_index = i;
				return VK_SUCCESS;
			}
		}
	};
}

static void * comp_wivrn_present_thread(void * void_param)
{
	std::unique_ptr<encoder_thread_param> param((encoder_thread_param *)void_param);
	struct wivrn_comp_target * cn = param->cn;
	struct vk_bundle * vk = get_vk(cn);
	U_LOG_I("Starting encoder thread %d", param->thread->index);

	uint8_t status_bit = 1 << (param->thread->index + 1);

	std::vector<VkFence> fences(cn->image_count);
	std::vector<int> indices(cn->image_count);
	while (os_thread_helper_is_running(&param->thread->thread))
	{
		int presenting_index = -1;
		int nb_fences = 0;
		{
			std::unique_lock lock(cn->psc.mutex);

			uint64_t timestamp = 0;

			for (uint32_t i = 0; i < cn->image_count; i++)
			{
				if (cn->psc.images[i].status & status_bit)
				{
					if (timestamp < cn->psc.images[i].view_info.display_time)
					{
						presenting_index = i;
					}
					indices[nb_fences] = i;
					fences[nb_fences++] = *cn->psc.images[i].fence;
				}
			}

			if (presenting_index < 0)
			{
				// condition variable is not notified when we want to stop thread,
				// use a timeout, but longer than a typical frame
				cn->psc.cv.wait_for(lock, std::chrono::milliseconds(50));
				continue;
			}
		}

		VkResult res = vk->vkWaitForFences(vk->device, nb_fences, fences.data(), VK_TRUE, UINT64_MAX);
		if (nb_fences > 1)
		{
			U_LOG_I("Encoder group %d dropped %d frames", param->thread->index, nb_fences - 1);
		}
		VK_CHK_WITH_RET(res, "vkWaitForFences", NULL);

		const auto & psc_image = cn->psc.images[presenting_index];
		try
		{
			for (auto & encoder: param->encoders)
			{
				encoder->Encode(*cn->cnx, psc_image.view_info, psc_image.frame_index);
			}
		}
		catch (...)
		{
			// Ignore errors
		}

		std::lock_guard lock(cn->psc.mutex);
		for (int i = 0; i < nb_fences; i++)
		{
			cn->psc.images[indices[i]].status &= ~status_bit;
		}
	}

	return NULL;
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

	struct vk_bundle * vk = get_vk(cn);

	auto & command_buffer = cn->psc.images[index].command_buffer;
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

	if (cn->c->base.slot.layer_count == 0)
	{
		// TODO: Tell the headset that there is no image to display
		assert(cn->psc.images[index].status == image_acquired);
		command_buffer.end();
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info);
		cn->psc.images[index].status = image_free;
		return VK_SUCCESS;
	}

	assert(index < ct->image_count);
	assert(ct->images != NULL);

	auto & yuv = cn->psc.images[index].yuv;
	yuv.record_draw_commands(command_buffer);
	for (auto & encoder: cn->encoders)
	{
		encoder->PresentImage(yuv, command_buffer);
	}
	command_buffer.end();

	std::lock_guard lock(cn->psc.mutex);
	cn->wivrn_bundle->device.resetFences(*cn->psc.images[index].fence);
	{
		scoped_lock lock(vk->queue_mutex);
		cn->wivrn_bundle->queue.submit(submit_info, *cn->psc.images[index].fence);
	}

	assert(cn->psc.images[index].status == image_acquired);
	cn->frame_index++;
	// set bits to 1 for index 1..num encoder threads + 1
	cn->psc.images[index].status = (1 << (cn->encoder_threads.size() + 1)) - 2;
	cn->psc.images[index].frame_index = cn->frame_index;

	auto & view_info = cn->psc.images[index].view_info;
	view_info.display_time = cn->cnx->get_offset().to_headset(desired_present_time_ns).count();
	for (int eye = 0; eye < 2; ++eye)
	{
		xrt_relation_chain xrc{};
		xrt_space_relation result{};
		m_relation_chain_push_pose_if_not_identity(&xrc, &cn->c->base.slot.poses[eye]);
		m_relation_chain_resolve(&xrc, &result);
		view_info.fov[eye] = xrt_cast(cn->c->base.slot.fovs[eye]);
		view_info.pose[eye] = xrt_cast(result.pose);
	}
	cn->psc.cv.notify_all();

	return VK_SUCCESS;
}

static void comp_wivrn_flush(struct comp_target * ct)
{
	(void)ct;
}

static void comp_wivrn_calc_frame_pacing(struct comp_target * ct,
                                         int64_t * out_frame_id,
                                         uint64_t * out_wake_up_time_ns,
                                         uint64_t * out_desired_present_time_ns,
                                         uint64_t * out_present_slop_ns,
                                         uint64_t * out_predicted_display_time_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	int64_t frame_id /*= ++cn->current_frame_id*/; //-1;
	uint64_t desired_present_time_ns /*= cn->next_frame_timestamp*/;
	uint64_t wake_up_time_ns /*= desired_present_time_ns - 5 * U_TIME_1MS_IN_NS*/;
	uint64_t present_slop_ns /*= U_TIME_HALF_MS_IN_NS*/;
	uint64_t predicted_display_time_ns /*= desired_present_time_ns + 5 * U_TIME_1MS_IN_NS*/;

#if 1
	uint64_t predicted_display_period_ns = U_TIME_1S_IN_NS / 60;
	uint64_t min_display_period_ns = predicted_display_period_ns;
	uint64_t now_ns = os_monotonic_get_ns();

	u_pc_predict(cn->upc,                      //
	             now_ns,                       //
	             &frame_id,                    //
	             &wake_up_time_ns,             //
	             &desired_present_time_ns,     //
	             &present_slop_ns,             //
	             &predicted_display_time_ns,   //
	             &predicted_display_period_ns, //
	             &min_display_period_ns);      //

	cn->current_frame_id = frame_id;

#endif

	*out_frame_id = frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_present_slop_ns = present_slop_ns;
}

static void comp_wivrn_mark_timing_point(struct comp_target * ct,
                                         enum comp_target_timing_point point,
                                         int64_t frame_id,
                                         uint64_t when_ns)
{
	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;
	assert(frame_id == cn->current_frame_id);

	switch (point)
	{
		case COMP_TARGET_TIMING_POINT_WAKE_UP:
			cn->cnx->dump_time("wake_up", cn->frame_index + 1, when_ns);
			u_pc_mark_point(cn->upc, U_TIMING_POINT_WAKE_UP, cn->current_frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_BEGIN:
			cn->cnx->dump_time("begin", cn->frame_index + 1, when_ns);
			u_pc_mark_point(cn->upc, U_TIMING_POINT_BEGIN, cn->current_frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_BEGIN:
			u_pc_mark_point(cn->upc, U_TIMING_POINT_SUBMIT_BEGIN, cn->current_frame_id, when_ns);
			break;
		case COMP_TARGET_TIMING_POINT_SUBMIT_END:
			cn->cnx->dump_time("submit", cn->frame_index + 1, when_ns);
			u_pc_mark_point(cn->upc, U_TIMING_POINT_SUBMIT_END, cn->current_frame_id, when_ns);
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

	struct wivrn_comp_target * cn = (struct wivrn_comp_target *)ct;

	u_pc_info_gpu(cn->upc, frame_id, gpu_start_ns, gpu_end_ns, when_ns);
}

void wivrn_comp_target::on_feedback(const from_headset::feedback & feedback, const clock_offset & o)
{
	if (not feedback.sent_to_decoder)
	{
		if (encoders.size() < feedback.stream_index)
			return;
		encoders[feedback.stream_index]->SyncNeeded();
	}
}

wivrn_comp_target::wivrn_comp_target(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, struct comp_compositor * c, float fps) :
        comp_target{},
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
	this->c = c;
}
