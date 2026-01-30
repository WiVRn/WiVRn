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

#include "encoder/video_encoder.h"
#include "encoder_settings.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <stdexcept>

namespace wivrn
{

void video_encoder_x264::ProcessCb(x264_t * h, x264_nal_t * nal, void * opaque)
{
	video_encoder_x264 * self = (video_encoder_x264 *)opaque;
	std::vector<uint8_t> data(nal->i_payload * 3 / 2 + 5 + 64, 0);
	x264_nal_encode(h, data.data(), nal);
	data.resize(nal->i_payload);
	switch (nal->i_type)
	{
		case NAL_SPS:
		case NAL_PPS: {
			self->SendData(data, false, self->control);
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

void video_encoder_x264::ProcessNal(pending_nal && nal)
{
	std::lock_guard lock(mutex);
	if (nal.first_mb == next_mb)
	{
		next_mb = nal.last_mb + 1;
		SendData(nal.data, next_mb == num_mb, control);
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

void video_encoder_x264::InsertInPendingNal(pending_nal && nal)
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

video_encoder_x264::video_encoder_x264(
        wivrn_vk_bundle & vk,
        const encoder_settings & settings,
        uint8_t stream_idx) :
        video_encoder(stream_idx, settings, std::make_unique<default_idr_handler>(), false)
{
	if (settings.bit_depth != 8)
		throw std::runtime_error("x264 encoder only supports 8-bit encoding");

	if (settings.codec != h264)
		U_LOG_W("requested x264 encoder with codec != h264");

	// encoder requires width and height to be even
	chroma_width = extent.width / 2;

	num_mb = ((extent.width + 15) / 16) * ((extent.height + 15) / 16);

	x264_param_default_preset(&param, "ultrafast", "zerolatency");
	param.nalu_process = &ProcessCb;
	// param.i_slice_max_size = 1300;
	param.i_slice_count = 32;
	param.i_width = extent.width;
	param.i_height = extent.height;
	param.i_log_level = X264_LOG_WARNING;
	param.i_fps_num = settings.fps * 1'000'000;
	param.i_fps_den = 1'000'000;
	param.b_repeat_headers = 1;
	param.b_aud = 0;
	param.i_keyint_max = X264_KEYINT_MAX_INFINITE;

	// colour definitions, actually ignored by decoder
	param.vui.b_fullrange = 1;
	param.vui.i_colorprim = 1; // BT.709
	param.vui.i_colmatrix = 1; // BT.709
	param.vui.i_transfer = 13; // sRGB

	param.vui.i_sar_width = extent.width;
	param.vui.i_sar_height = extent.height;
	param.rc.i_rc_method = X264_RC_ABR;
	param.rc.i_bitrate = settings.bitrate / 1000; // x264 uses kbit/s
	param.rc.i_vbv_max_bitrate = param.rc.i_bitrate;
	param.rc.i_vbv_buffer_size = param.rc.i_bitrate / settings.fps * 1.1;

	x264_param_apply_profile(&param, "main");

	enc = x264_encoder_open(&param);
	if (not enc)
	{
		throw std::runtime_error("failed to create x264 encoder");
	}

	assert(x264_encoder_maximum_delayed_frames(enc) == 0);

	for (auto & i: in)
	{
		i.luma = buffer_allocation(
		        vk.device,
		        {
		                .size = vk::DeviceSize(extent.width * extent.height),
		                .usage = vk::BufferUsageFlagBits::eTransferDst,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "x264 luma buffer");
		i.chroma = buffer_allocation(
		        vk.device, {
		                           .size = vk::DeviceSize(extent.width * extent.height / 2),
		                           .usage = vk::BufferUsageFlagBits::eTransferDst,
		                   },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "x264 chroma buffer");

		auto & pic = i.pic;
		x264_picture_init(&pic);
		pic.opaque = this;
		pic.img.i_csp = X264_CSP_NV12;
		pic.img.i_plane = 2;

		pic.img.i_stride[0] = extent.width;
		pic.img.plane[0] = (uint8_t *)i.luma.map();
		pic.img.i_stride[1] = extent.width;
		pic.img.plane[1] = (uint8_t *)i.chroma.map();
	}
}

std::pair<bool, vk::Semaphore> video_encoder_x264::present_image(vk::Image y_cbcr, bool transferred, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t)
{
	cmd_buf.copyImageToBuffer(
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        in[slot].luma,
	        vk::BufferImageCopy{
	                .bufferRowLength = chroma_width * 2,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .baseArrayLayer = stream_idx,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width,
	                        .height = extent.height,
	                        .depth = 1,
	                }});
	cmd_buf.copyImageToBuffer(
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        in[slot].chroma,
	        vk::BufferImageCopy{
	                .bufferRowLength = chroma_width,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .baseArrayLayer = stream_idx,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width / 2,
	                        .height = extent.height / 2,
	                        .depth = 1,
	                }});
	return {false, nullptr};
}

std::optional<video_encoder::data> video_encoder_x264::encode(uint8_t slot, uint64_t frame_index)
{
	bool reconfigure = false;
	if (auto framerate = pending_framerate.exchange(0))
	{
		reconfigure = true;
		param.i_fps_num = framerate * 1'000'000;
		param.i_fps_den = 1'000'000;
	}
	if (auto bitrate = pending_bitrate.exchange(0))
	{
		reconfigure = true;
		auto fps_mul = param.i_fps_num / (float)param.i_fps_den;
		param.rc.i_bitrate = bitrate / 1000;
		param.rc.i_vbv_buffer_size = param.rc.i_bitrate / fps_mul * 1.1;
		param.rc.i_vbv_max_bitrate = param.rc.i_bitrate;
	}
	auto & idr_handler = ((default_idr_handler &)*idr);
	if (reconfigure)
	{
		x264_encoder_reconfig(enc, &param);
		idr->reset();
	}
	auto frame_type = idr_handler.get_type(frame_index);
	int num_nal;
	x264_nal_t * nal;
	auto & pic = in[slot].pic;
	switch (frame_type)
	{
		case default_idr_handler::frame_type::i:
			control = true;
			pic.i_type = X264_TYPE_IDR;
			break;
		case default_idr_handler::frame_type::p:
			control = false;
			pic.i_type = X264_TYPE_P;
			break;
	}
	next_mb = 0;
	assert(pending_nals.empty());
	int size = x264_encoder_encode(enc, &nal, &num_nal, &pic, &pic_out);
	if (next_mb != num_mb)
	{
		U_LOG_W("unexpected macroblock count: %d", next_mb);
	}
	if (size < 0)
	{
		U_LOG_W("x264_encoder_encode failed: %d", size);
	}
	return {};
}

video_encoder_x264::~video_encoder_x264()
{
	x264_encoder_close(enc);
}

} // namespace wivrn
