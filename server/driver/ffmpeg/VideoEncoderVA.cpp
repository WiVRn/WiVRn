// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "VideoEncoderVA.h"
#include "ffmpeg_helper.h"
#include "vk/vk_helpers.h"
#include <cassert>
#include <stdexcept>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/opt.h>
}

namespace {

const char *
encoder(VideoEncoderFFMPEG::Codec codec)
{
	switch (codec) {
	case VideoEncoderFFMPEG::Codec::h264: return "h264_vaapi";
	case VideoEncoderFFMPEG::Codec::h265: return "hevc_vaapi";
	}
	throw std::runtime_error("invalid codec " + std::to_string(int(codec)));
}

void
set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx)
{
	av_buffer_ptr hw_frames_ref(av_hwframe_ctx_alloc(hw_device_ctx));
	int err = 0;

	if (not hw_frames_ref) {
		throw std::runtime_error("Failed to create VAAPI frame context.");
	}
	auto frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
	frames_ctx->format = AV_PIX_FMT_VAAPI;
	frames_ctx->sw_format = AV_PIX_FMT_NV12;
	frames_ctx->width = ctx->width;
	frames_ctx->height = ctx->height;
	frames_ctx->initial_pool_size = 3;
	if ((err = av_hwframe_ctx_init(hw_frames_ref.get())) < 0) {
		throw std::system_error(err, av_error_category(), "Failed to initialize VAAPI frame context");
	}
	ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref.get());
	if (!ctx->hw_frames_ctx)
		err = AVERROR(ENOMEM);
}

std::string
get_render_device(vk_bundle *vk)
{
	VkPhysicalDeviceDrmPropertiesEXT drmProps{};
	drmProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;

	VkPhysicalDeviceProperties2 props{};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &drmProps;

	vk->vkGetPhysicalDeviceProperties2(vk->physical_device, &props);

	if (!drmProps.hasRender) {
		U_LOG_E("Failed to find render DRM device");
		throw std::runtime_error("Failed to find render DRM device");
	}
	return "/dev/dri/renderD" + std::to_string(drmProps.renderMinor);
}

} // namespace

VideoEncoderVA::VideoEncoderVA(vk_bundle *vk, const encoder_settings &settings, float fps)
    : vk(vk), width(settings.width), height(settings.height), offset_x(settings.offset_x), offset_y(settings.offset_y)
{
	codec = settings.codec;
	/* VAAPI Encoding pipeline
	 * The encoding pipeline has 3 frame types:
	 * - input vulkan frames, only used to initialize the mapped frames
	 * - mapped frames, one per input frame, same format, and point to the same memory on the device
	 * - encoder frame, with a format compatible with the encoder, created by the filter
	 * Each frame type has a corresponding hardware frame context, the vulkan one is provided
	 *
	 * The pipeline is simply made of a scale_vaapi object, that does the conversion between formats
	 * and the encoder that takes the converted frame and produces packets.
	 */

	AVBufferRef *tmp;
	const std::string device = get_render_device(vk);
	int err = av_hwdevice_ctx_create(&tmp, AV_HWDEVICE_TYPE_VAAPI, device.c_str(), NULL, 0);
	if (err < 0) {
		throw std::system_error(err, av_error_category(), "Failed create a VAAPI device");
	}
	hw_ctx_vaapi.reset(tmp);

	const char *encoder_name = encoder(codec);
	const AVCodec *codec = avcodec_find_encoder_by_name(encoder_name);
	if (codec == nullptr) {
		throw std::runtime_error(std::string("Failed to find encoder ") + encoder_name);
	}

	encoder_ctx.reset(avcodec_alloc_context3(codec));
	if (not encoder_ctx) {
		throw std::runtime_error("failed to allocate VAAPI encoder");
	}

	AVDictionary *opts = nullptr;
	for (auto option : settings.options) {
		av_dict_set(&opts, option.first.c_str(), option.second.c_str(), 0);
	}
	switch (this->codec) {
	case Codec::h264: encoder_ctx->profile = FF_PROFILE_H264_MAIN; break;
	case Codec::h265: encoder_ctx->profile = FF_PROFILE_HEVC_MAIN; break;
	}

	encoder_ctx->width = width;
	encoder_ctx->height = height;
	encoder_ctx->time_base = {std::chrono::steady_clock::duration::period::num,
	                          std::chrono::steady_clock::duration::period::den};
	encoder_ctx->framerate = AVRational{(int)fps, 1};
	encoder_ctx->sample_aspect_ratio = AVRational{1, 1};
	encoder_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
	encoder_ctx->max_b_frames = 0;
	encoder_ctx->bit_rate = settings.bitrate;

	set_hwframe_ctx(encoder_ctx.get(), hw_ctx_vaapi.get());

	err = avcodec_open2(encoder_ctx.get(), codec, &opts);
	av_dict_free(&opts);
	if (err < 0) {
		throw std::system_error(err, av_error_category(), "Cannot open video encoder codec");
	}
}

void
VideoEncoderVA::PushFrame(uint32_t frame_index, bool idr, std::chrono::steady_clock::time_point pts)
{
	assert(frame_index < mapped_frames.size());
	av_frame_ptr encoder_frame = make_av_frame();
	int err = av_buffersrc_add_frame_flags(filter_in, mapped_frames[frame_index].get(),
	                                       AV_BUFFERSRC_FLAG_PUSH | AV_BUFFERSRC_FLAG_KEEP_REF);
	if (err != 0) {
		throw std::system_error(err, av_error_category(), "av_buffersrc_add_frame failed");
	}
	err = av_buffersink_get_frame(filter_out, encoder_frame.get());
	if (err != 0) {
		throw std::system_error(err, av_error_category(), "av_buffersink_get_frame failed");
	}

	encoder_frame->pict_type = idr ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
	encoder_frame->pts = pts.time_since_epoch().count();
	err = avcodec_send_frame(encoder_ctx.get(), encoder_frame.get());
	if (err) {
		throw std::system_error(err, av_error_category(), "avcodec_send_frame failed");
	}
}

void
VideoEncoderVA::SetImages(
    int width, int height, VkFormat format, int num_images, VkImage *images, VkImageView *views, VkDeviceMemory *memory)
{
	mapped_frames.clear();
	av_buffer_ptr va_ctx(av_hwframe_ctx_alloc(hw_ctx_vaapi.get()));
	if (!va_ctx) {
		throw std::runtime_error("Failed to create VAAPI frame context.");
	}
	AVHWFramesContext *va_frames_ctx = (AVHWFramesContext *)(va_ctx->data);
	va_frames_ctx->format = AV_PIX_FMT_VAAPI;
	va_frames_ctx->sw_format = vk_format_to_av_format(format);
	va_frames_ctx->width = width;
	va_frames_ctx->height = height;
	va_frames_ctx->initial_pool_size = num_images;
	int err = av_hwframe_ctx_init(va_ctx.get());
	if (err) {
		throw std::system_error(err, av_error_category(), "Failed to create VAAPI frame context");
	}

	mapped_frames.reserve(num_images);
	for (int i = 0; i < num_images; ++i) {
		VkMemoryRequirements req;
		vk->vkGetImageMemoryRequirements(vk->device, images[i], &req);

		int fd;
		VkMemoryGetFdInfoKHR export_info{};
		export_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
		export_info.memory = memory[i];
		export_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		vk->vkGetMemoryFdKHR(vk->device, &export_info, &fd);

		auto drm_frame = make_av_frame();
		drm_frame->format = AV_PIX_FMT_DRM_PRIME;
		drm_frame->width = width;
		drm_frame->height = height;
		drm_frame->buf[0] = av_buffer_alloc(sizeof(AVDRMFrameDescriptor));
		drm_frame->data[0] = drm_frame->buf[0]->data;
		auto &drm_frame_desc = *(AVDRMFrameDescriptor *)drm_frame->data[0];
		drm_frame_desc.nb_objects = 1;
		drm_frame_desc.objects[0].fd = fd;
		drm_frame_desc.objects[0].size = req.size;
		drm_frame_desc.nb_layers = 1;
		drm_frame_desc.layers[0].format = vk_format_to_fourcc(format);
		drm_frame_desc.layers[0].nb_planes = 1;
		drm_frame_desc.layers[0].planes[0].object_index = 0;

		// DRM format modifiers seem to be broken on ffmpeg or radeon, or both
		bool drm_tiling = use_drm_format_modifiers and vk->has_EXT_image_drm_format_modifier;
		if (drm_tiling) {
			VkImageDrmFormatModifierPropertiesEXT modifiers{};
			modifiers.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
			vk->vkGetImageDrmFormatModifierPropertiesEXT(vk->device, images[i], &modifiers);
			drm_frame_desc.objects[0].format_modifier = modifiers.drmFormatModifier;
		}
		// if we don't have modifiers, we did linear tiling
		VkImageSubresource subresource{(use_drm_format_modifiers and vk->has_EXT_image_drm_format_modifier)
		                                   ? VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT
		                                   : VK_IMAGE_ASPECT_COLOR_BIT,
		                               0, 0};
		VkSubresourceLayout layout;
		vk->vkGetImageSubresourceLayout(vk->device, images[i], &subresource, &layout);
		drm_frame_desc.layers[0].planes[0].offset = layout.offset;
		drm_frame_desc.layers[0].planes[0].pitch = layout.rowPitch;

		auto va_frame = make_av_frame();
		err = av_hwframe_get_buffer(va_ctx.get(), va_frame.get(), 0);
		if (err)
			throw std::system_error(err, av_error_category(), "Failed to create VAAPI frame");
		err = av_hwframe_map(va_frame.get(), drm_frame.get(), AV_HWFRAME_MAP_DIRECT);
		if (err)
			throw std::system_error(err, av_error_category(), "Failed to map DRM frame to VAAPI frame");
		va_frame->crop_left = offset_x;
		va_frame->crop_right = width - this->width - offset_x;
		va_frame->crop_top = offset_y;
		va_frame->crop_bottom = height - this->height - offset_y;
		mapped_frames.push_back(std::move(va_frame));
	}

	InitFilterGraph();
}

void
VideoEncoderVA::InitFilterGraph()
{
	assert(not mapped_frames.empty());

	filter_graph.reset(avfilter_graph_alloc());

	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();

	filter_in = avfilter_graph_alloc_filter(filter_graph.get(), avfilter_get_by_name("buffer"), "in");

	AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
	memset(par, 0, sizeof(*par));
	par->width = mapped_frames[0]->width;
	par->height = mapped_frames[0]->height;
	par->time_base = encoder_ctx->time_base;
	par->format = mapped_frames[0]->format;
	par->hw_frames_ctx = av_buffer_ref(mapped_frames[0]->hw_frames_ctx);
	av_buffersrc_parameters_set(filter_in, par);
	av_free(par);

	int err;
	if ((err = avfilter_graph_create_filter(&filter_out, avfilter_get_by_name("buffersink"), "out", NULL, NULL,
	                                        filter_graph.get()))) {
		throw std::system_error(err, av_error_category(), "filter_out creation failed");
	}

	outputs->name = av_strdup("in");
	outputs->filter_ctx = filter_in;
	outputs->pad_idx = 0;
	outputs->next = NULL;

	inputs->name = av_strdup("out");
	inputs->filter_ctx = filter_out;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	std::string scale_param = "scale_vaapi=format=nv12:w=" + std::to_string(encoder_ctx->width) +
	                          ":h=" + std::to_string(encoder_ctx->height);
	err = avfilter_graph_parse_ptr(filter_graph.get(), scale_param.c_str(), &inputs, &outputs, NULL);
	avfilter_inout_free(&outputs);
	avfilter_inout_free(&inputs);
	if (err < 0) {
		throw std::system_error(err, av_error_category(), "avfilter_graph_parse_ptr failed");
	}

	if ((err = avfilter_graph_config(filter_graph.get(), NULL))) {
		throw std::system_error(err, av_error_category(), "avfilter_graph_config failed");
	}
}
