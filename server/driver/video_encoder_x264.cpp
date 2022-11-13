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
		case NAL_PPS:
			self->SendData(std::move(data));
			break;
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
		SendData(std::move(nal.data));
	}
	else
	{
		InsertInPendingNal(std::move(nal));
	}
	while ((not pending_nals.empty()) and pending_nals.front().first_mb == next_mb)
	{
		SendData(std::move(pending_nals.front().data));
		next_mb = pending_nals.front().last_mb + 1;
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
        vk_bundle * vk,
        encoder_settings & settings,
        int input_width,
        int input_height,
        float fps) :
        vk(vk)
{
	if (settings.codec != h264)
	{
		U_LOG_W("requested x264 encoder with codec != h264");
		settings.codec = h264;
	}

	// encoder requires width and height to be even
	settings.width += settings.width % 2;
	settings.height += settings.height % 2;

	converter =
	        std::make_unique<YuvConverter>(vk, VkExtent3D{uint32_t(settings.width), uint32_t(settings.height), 1}, settings.offset_x, settings.offset_y, input_width, input_height);

	x264_param_default_preset(&param, "ultrafast", "zerolatency");
	param.nalu_process = &ProcessCb;
	// param.i_slice_max_size = 1300;
	param.i_slice_count = 32;
	param.i_width = settings.width;
	param.i_height = settings.height;
	param.i_log_level = X264_LOG_WARNING;
	param.i_fps_num = fps * 1'000'000;
	param.i_fps_den = 1'000'000;
	param.b_repeat_headers = 1;
	param.b_aud = 0;

	// colour definitions, actually ignored by decoder
	using namespace to_headset;
	param.vui.b_fullrange = 0;
	param.vui.i_colorprim = 1; // BT.709
	param.vui.i_colmatrix = 1; // BT.709
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

	pic.img.i_stride[0] = converter->y.stride;
	pic.img.plane[0] = (uint8_t *)converter->y.mapped_memory;
	pic.img.i_stride[1] = converter->uv.stride;
	pic.img.plane[1] = (uint8_t *)converter->uv.mapped_memory;
}

void VideoEncoderX264::SetImages(int width, int height, VkFormat format, int num_images, VkImage * images, VkImageView * views, VkDeviceMemory * memory)
{
	converter->SetImages(num_images, images, views);
}

void VideoEncoderX264::PresentImage(int index, VkCommandBuffer * out_buffer)
{
	*out_buffer = converter->command_buffers[index];
}

void VideoEncoderX264::Encode(int index, bool idr, std::chrono::steady_clock::time_point pts)
{
	int num_nal;
	x264_nal_t * nal;
	pic_in.i_type = idr ? X264_TYPE_IDR : X264_TYPE_AUTO;
	pic_in.i_pts = pts.time_since_epoch().count();
	next_mb = 0;
	assert(pending_nals.empty());
	int size = x264_encoder_encode(enc, &nal, &num_nal, &pic_in, &pic_out);
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
