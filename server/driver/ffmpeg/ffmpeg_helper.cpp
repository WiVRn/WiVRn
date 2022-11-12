// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "ffmpeg_helper.h"
#include <libdrm/drm_fourcc.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/opt.h>
}

namespace {
struct : public std::error_category
{
	const char *
	name() const noexcept override
	{
		return "ffmpeg";
	}
	std::string
	message(int averror) const override
	{
		char av_msg[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(averror, av_msg, sizeof(av_msg));
		return av_msg;
	}
} error_category;
} // namespace

const std::error_category &
av_error_category()
{
	return error_category;
};

// it seems that ffmpeg does not provide this mapping
AVPixelFormat
vk_format_to_av_format(VkFormat vk_fmt)
{
	switch (vk_fmt) {
	case VK_FORMAT_B8G8R8A8_SRGB: return AV_PIX_FMT_BGRA;
	default: break;
	}
	for (int f = AV_PIX_FMT_NONE; f < AV_PIX_FMT_NB; ++f) {
		auto current_fmt = av_vkfmt_from_pixfmt(AVPixelFormat(f));
		if (current_fmt and *current_fmt == (VkFormat)vk_fmt)
			return AVPixelFormat(f);
	}
	throw std::runtime_error("unsupported vulkan pixel format " + std::to_string((VkFormat)vk_fmt));
}

uint32_t
vk_format_to_fourcc(VkFormat vk_fmt)
{
	switch (vk_fmt) {
	case VK_FORMAT_B8G8R8A8_SRGB: return DRM_FORMAT_ARGB8888;
	case VK_FORMAT_B8G8R8A8_UNORM: return DRM_FORMAT_ARGB8888;
	default: break;
	}
	throw std::runtime_error("unsupported vulkan pixel format " + std::to_string((VkFormat)vk_fmt));
}

void
AvDeleter::operator()(AVBufferRef *x)
{
	av_buffer_unref(&x);
}

void
AvDeleter::operator()(AVFrame *x)
{
	av_frame_unref(x);
}

void
AvDeleter::operator()(AVCodecContext *x)
{
	avcodec_free_context(&x);
}

void
AvDeleter::operator()(AVFilterGraph *x)
{
	avfilter_graph_free(&x);
}

av_frame_ptr
make_av_frame(AVFrame *frame)
{
	return av_frame_ptr(frame);
}

av_frame_ptr
make_av_frame()
{
	return make_av_frame(av_frame_alloc());
}
