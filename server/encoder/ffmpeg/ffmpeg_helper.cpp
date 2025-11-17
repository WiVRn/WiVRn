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

#include "ffmpeg_helper.h"
#include <libdrm/drm_fourcc.h>
#include <vulkan/vulkan.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace
{
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
// todo: for monado async reprojection, VK_FORMAT_B8G8R8A8_UNORM has to be supported
AVPixelFormat
vk_format_to_av_format(VkFormat vk_fmt)
{
	switch (vk_fmt)
	{
		case VK_FORMAT_B8G8R8A8_SRGB:
			return AV_PIX_FMT_BGRA;
		default:
			break;
	}
	throw std::runtime_error("unsupported vulkan pixel format " + std::to_string((VkFormat)vk_fmt));
}

uint32_t
vk_format_to_fourcc(VkFormat vk_fmt)
{
	switch (vk_fmt)
	{
		case VK_FORMAT_B8G8R8A8_SRGB:
			return DRM_FORMAT_ARGB8888;
		case VK_FORMAT_B8G8R8A8_UNORM:
			return DRM_FORMAT_ARGB8888;
		default:
			break;
	}
	throw std::runtime_error("unsupported vulkan pixel format " + std::to_string((VkFormat)vk_fmt));
}

void AvDeleter::operator()(AVBufferRef * x)
{
	av_buffer_unref(&x);
}

void AvDeleter::operator()(AVFrame * x)
{
	av_frame_unref(x);
}

void AvDeleter::operator()(AVCodecContext * x)
{
	avcodec_free_context(&x);
}

void AvDeleter::operator()(AVPacket * x)
{
	av_packet_free(&x);
}

av_frame_ptr
make_av_frame(AVFrame * frame)
{
	return av_frame_ptr(frame);
}

av_frame_ptr
make_av_frame()
{
	return make_av_frame(av_frame_alloc());
}
