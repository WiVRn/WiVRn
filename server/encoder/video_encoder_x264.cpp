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

#include "video_encoder_x264.h"

#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"
#include "yuv_converter.h"
#include <stdexcept>

namespace xrt::drivers::wivrn
{

void VideoEncoderX264::ProcessCb(x264_t * h, x264_nal_t * nal, void * opaque)
{
	VideoEncoderX264 * self = (VideoEncoderX264 *)opaque;
	std::vector<uint8_t> data(nal->i_payload * 3 / 2 + 5 + 64, 0);
	x264_nal_encode(h, data.data(), nal);
	data.resize(nal->i_payload);
	switch (nal->i_type)
	{
		case NAL_SPS:
		case NAL_PPS: {
			self->SendData(data, false);
			break;
		}
		case NAL_SLICE:
		case NAL_SLICE_DPA:
		case NAL_SLICE_DPB:
		case NAL_SLICE_DPC:
		case NAL_SLICE_IDR:
			self->ProcessNal({nal->i_first_mb, nal->i_last_mb, std::move(data)});
	}
}

void VideoEncoderX264::ProcessNal(pending_nal && nal)
{
	std::lock_guard lock(mutex);
	if (nal.first_mb == next_mb)
	{
		next_mb = nal.last_mb + 1;
		SendData(nal.data, next_mb == num_mb);
	}
	else
	{
		InsertInPendingNal(std::move(nal));
	}
	while ((not pending_nals.empty()) and pending_nals.front().first_mb == next_mb)
	{
		next_mb = pending_nals.front().last_mb + 1;
		SendData(pending_nals.front().data, next_mb == num_mb);
		pending_nals.pop_front();
	}
}

void VideoEncoderX264::InsertInPendingNal(pending_nal && nal)
{
	auto it = pending_nals.begin();
	auto end = pending_nals.end();
	for (; it != end; ++it)
	{
		if (it->first_mb > nal.last_mb)
		{
			pending_nals.insert(it, std::move(nal));
			return;
		}
	}
	pending_nals.push_back(std::move(nal));
}

VideoEncoderX264::VideoEncoderX264(
        wivrn_vk_bundle & vk,
        encoder_settings & settings,
        float fps)
{
	if (settings.codec != h264)
	{
		U_LOG_W("requested x264 encoder with codec != h264");
		settings.codec = h264;
	}

	// encoder requires width and height to be even
	settings.video_width += settings.video_width % 2;
	settings.video_height += settings.video_height % 2;
	chroma_width = settings.video_width / 2;

	// FIXME: enforce even values
	rect = vk::Rect2D{
	        .offset = {
	                .x = settings.offset_x,
	                .y = settings.offset_y,
	        },
	        .extent = {
	                .width = settings.width,
	                .height = settings.height,
	        },
	};

	num_mb = ((settings.video_width + 15) / 16) * ((settings.video_height + 15) / 16);

	luma = buffer_allocation(
	        vk.device,
	        {
	                .size = vk::DeviceSize(settings.video_width * settings.video_height),
	                .usage = vk::BufferUsageFlagBits::eTransferDst,
	        },
	        {
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        });
	chroma = buffer_allocation(
	        vk.device, {
	                           .size = vk::DeviceSize(settings.video_width * settings.video_height / 2),
	                           .usage = vk::BufferUsageFlagBits::eTransferDst,
	                   },
	        {
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        });

	x264_param_default_preset(&param, "ultrafast", "zerolatency");
	param.nalu_process = &ProcessCb;
	// param.i_slice_max_size = 1300;
	param.i_slice_count = 32;
	param.i_width = settings.video_width;
	param.i_height = settings.video_height;
	param.i_log_level = X264_LOG_WARNING;
	param.i_fps_num = fps * 1'000'000;
	param.i_fps_den = 1'000'000;
	param.b_repeat_headers = 1;
	param.b_aud = 0;
	param.i_keyint_max = X264_KEYINT_MAX_INFINITE;

	// colour definitions, actually ignored by decoder
	param.vui.b_fullrange = 1;
	settings.range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
	param.vui.i_colorprim = 1; // BT.709
	param.vui.i_colmatrix = 1; // BT.709
	settings.color_model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
	param.vui.i_transfer = 13; // sRGB

	param.vui.i_sar_width = settings.width;
	param.vui.i_sar_height = settings.height;
	param.rc.i_rc_method = X264_RC_ABR;
	param.rc.i_bitrate = settings.bitrate / 1000; // x264 uses kbit/s
	enc = x264_encoder_open(&param);
	if (not enc)
	{
		throw std::runtime_error("failed to create x264 encoder");
	}

	assert(x264_encoder_maximum_delayed_frames(enc) == 0);

	auto & pic = pic_in;
	x264_picture_init(&pic);
	pic.opaque = this;
	pic.img.i_csp = X264_CSP_NV12;
	pic.img.i_plane = 2;

	pic.img.i_stride[0] = settings.video_width;
	pic.img.plane[0] = (uint8_t *)luma.map();
	pic.img.i_stride[1] = settings.video_width;
	pic.img.plane[1] = (uint8_t *)chroma.map();
}

void VideoEncoderX264::PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf)
{
	cmd_buf.copyImageToBuffer(
	        src_yuv.luma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        luma,
	        vk::BufferImageCopy{
	                .bufferRowLength = chroma_width * 2,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .imageOffset = {
	                        .x = rect.offset.x,
	                        .y = rect.offset.y,
	                },
	                .imageExtent = {
	                        .width = rect.extent.width,
	                        .height = rect.extent.height,
	                        .depth = 1,
	                }});
	cmd_buf.copyImageToBuffer(
	        src_yuv.chroma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        chroma,
	        vk::BufferImageCopy{
	                .bufferRowLength = chroma_width,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .imageOffset = {
	                        .x = rect.offset.x / 2,
	                        .y = rect.offset.y / 2,
	                },
	                .imageExtent = {
	                        .width = rect.extent.width / 2,
	                        .height = rect.extent.height / 2,
	                        .depth = 1,
	                }});
}

void VideoEncoderX264::Encode(bool idr, std::chrono::steady_clock::time_point pts)
{
	int num_nal;
	x264_nal_t * nal;
	pic_in.i_type = idr ? X264_TYPE_IDR : X264_TYPE_P;
	pic_in.i_pts = pts.time_since_epoch().count();
	next_mb = 0;
	assert(pending_nals.empty());
	int size = x264_encoder_encode(enc, &nal, &num_nal, &pic_in, &pic_out);
	if (next_mb != num_mb)
	{
		U_LOG_W("unexpected macroblock count: %d", next_mb);
	}
	if (size < 0)
	{
		U_LOG_W("x264_encoder_encode failed: %d", size);
		return;
	}
	if (size == 0)
	{
		return;
	}
}

VideoEncoderX264::~VideoEncoderX264()
{
	x264_encoder_close(enc);
}

} // namespace xrt::drivers::wivrn
