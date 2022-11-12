// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>
#include "vk/vk_helpers.h"

struct AVBufferRef;
struct AVFilterGraph;
struct AVFrame;
struct AVCodecContext;

extern "C" {
#include <libavutil/avutil.h>
}

const std::error_category &
av_error_category();

AVPixelFormat
vk_format_to_av_format(VkFormat vk_fmt);

uint32_t
vk_format_to_fourcc(VkFormat vk_fmt);

struct AvDeleter
{
	void
	operator()(AVBufferRef *);
	void
	operator()(AVFrame *);
	void
	operator()(AVCodecContext *);
	void
	operator()(AVFilterGraph *);
};

using av_buffer_ptr = std::unique_ptr<AVBufferRef, AvDeleter>;
using av_frame_ptr = std::unique_ptr<AVFrame, AvDeleter>;
using av_codec_context_ptr = std::unique_ptr<AVCodecContext, AvDeleter>;
using av_filter_graph_ptr = std::unique_ptr<AVFilterGraph, AvDeleter>;

av_buffer_ptr
make_av_buffer(AVBufferRef *);

av_frame_ptr
make_av_frame(AVFrame *);

av_frame_ptr
make_av_frame();
