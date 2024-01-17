/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

// must be included before vulkan_raii

#include "video_encoder_va.h"

#include "encoder/yuv_converter.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <drm_fourcc.h>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vulkan/vulkan_raii.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
}

namespace
{
const char *
encoder(VideoEncoderFFMPEG::Codec codec)
{
	switch (codec)
	{
		case VideoEncoderFFMPEG::Codec::h264:
			return "h264_vaapi";
		case VideoEncoderFFMPEG::Codec::h265:
			return "hevc_vaapi";
	}
	throw std::runtime_error("invalid codec " + std::to_string(int(codec)));
}

av_buffer_ptr make_hwframe_ctx(AVBufferRef * hw_device_ctx, AVPixelFormat hw_format, AVPixelFormat sw_format, int width, int height)
{
	av_buffer_ptr hw_frames_ref(av_hwframe_ctx_alloc(hw_device_ctx));

	if (not hw_frames_ref)
	{
		throw std::runtime_error("Failed to create VAAPI frame context.");
	}
	auto frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
	frames_ctx->format = hw_format;
	frames_ctx->sw_format = sw_format;
	frames_ctx->width = width;
	frames_ctx->height = height;
	frames_ctx->initial_pool_size = 10;
	if (int err = av_hwframe_ctx_init(hw_frames_ref.get()); err < 0)
	{
		throw std::system_error(err, av_error_category(), "Failed to initialize frame context");
	}
	return std::move(hw_frames_ref);
}

std::optional<std::filesystem::path>
get_render_device(vk::raii::PhysicalDevice & physical_device)
{
	auto [props, drm_props] = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDrmPropertiesEXT>();

	if (!drm_props.hasRender)
	{
		U_LOG_E("Failed to find render DRM device");
		throw std::runtime_error("Failed to find render DRM device");
	}
	std::filesystem::path path = "/dev/dri/renderD" + std::to_string(drm_props.renderMinor);
	if (not std::filesystem::exists(path))
	{
		U_LOG_W("DRI device %s does not exist, reverting to default", path.c_str());
		return std::nullopt;
	}
	return path;
}

av_buffer_ptr make_drm_hw_ctx(vk::raii::PhysicalDevice & physical_device)
{
	const auto device = get_render_device(physical_device);
	AVBufferRef * hw_ctx;
	int err = av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_DRM, device ? device->c_str() : NULL, NULL, 0);
	if (err)
		throw std::system_error(err, av_error_category(), "FFMPEG drm hardware context creation failed");
	return av_buffer_ptr(hw_ctx);
}

std::unordered_map<uint32_t, vk::Format> vulkan_drm_format_map = {
        {DRM_FORMAT_R8, vk::Format::eR8Unorm},
        {DRM_FORMAT_R16, vk::Format::eR16Unorm},
        {DRM_FORMAT_GR88, vk::Format::eR8G8Unorm},
        {DRM_FORMAT_RG88, vk::Format::eR8G8Unorm},
        {DRM_FORMAT_GR1616, vk::Format::eR16G16Unorm},
        {DRM_FORMAT_RG1616, vk::Format::eR16G16Unorm},
        {DRM_FORMAT_ARGB8888, vk::Format::eB8G8R8A8Unorm},
        {DRM_FORMAT_XRGB8888, vk::Format::eB8G8R8A8Unorm},
        {DRM_FORMAT_ABGR8888, vk::Format::eR8G8B8A8Unorm},
        {DRM_FORMAT_XBGR8888, vk::Format::eR8G8B8A8Unorm},
};

vk::Format drm_to_vulkan_fmt(uint32_t drm_fourcc)
{
	return vulkan_drm_format_map.at(drm_fourcc);
}

} // namespace

video_encoder_va::video_encoder_va(wivrn_vk_bundle & vk, xrt::drivers::wivrn::encoder_settings & settings, float fps) :
        luma(nullptr), chroma(nullptr)
{
	auto drm_hw_ctx = make_drm_hw_ctx(vk.physical_device);
	AVBufferRef * tmp;
	int err = av_hwdevice_ctx_create_derived(&tmp,
	                                         AV_HWDEVICE_TYPE_VAAPI,
	                                         drm_hw_ctx.get(),
	                                         0);
	if (err)
		throw std::system_error(err, av_error_category(), "FFMPEG vaapi hardware context creation failed");
	av_buffer_ptr vaapi_hw_ctx(tmp);

	settings.video_width += settings.video_width % 2;
	settings.video_height += settings.video_height % 2;

	auto vaapi_frame_ctx = make_hwframe_ctx(vaapi_hw_ctx.get(), AV_PIX_FMT_VAAPI, AV_PIX_FMT_NV12, settings.video_width, settings.video_height);

	assert(av_pix_fmt_count_planes(AV_PIX_FMT_NV12) == 2);

	rect = vk::Rect2D{
	        .offset = {
	                .x = settings.offset_x,
	                .y = settings.offset_y,
	        },
	        .extent = {
	                .width = settings.width,
	                .height = settings.height,
	        }};

	err = av_hwframe_ctx_create_derived(&tmp,
	                                    AV_PIX_FMT_DRM_PRIME,
	                                    drm_hw_ctx.get(),
	                                    vaapi_frame_ctx.get(),
	                                    AV_HWFRAME_MAP_DIRECT);
	if (err < 0)
	{
		throw std::system_error(err, av_error_category(), "Cannot create drm frame context");
	}
	drm_frame_ctx = av_buffer_ptr(tmp);

	const char * encoder_name = encoder(settings.codec);
	const AVCodec * codec = avcodec_find_encoder_by_name(encoder_name);
	if (codec == nullptr)
	{
		throw std::runtime_error(std::string("Failed to find encoder ") + encoder_name);
	}

	encoder_ctx = av_codec_context_ptr(avcodec_alloc_context3(codec));
	if (not encoder_ctx)
	{
		throw std::runtime_error("failed to allocate VAAPI encoder");
	}

	AVDictionary * opts = nullptr;
	av_dict_set(&opts, "async_depth", "1", 0);
	for (auto option: settings.options)
	{
		av_dict_set(&opts, option.first.c_str(), option.second.c_str(), 0);
	}
	switch (settings.codec)
	{
		case Codec::h264:
			encoder_ctx->profile = FF_PROFILE_H264_MAIN;
			break;
		case Codec::h265:
			encoder_ctx->profile = FF_PROFILE_HEVC_MAIN;
			break;
	}

	encoder_ctx->width = settings.video_width;
	encoder_ctx->height = settings.video_height;
	encoder_ctx->time_base = {std::chrono::steady_clock::duration::period::num,
	                          std::chrono::steady_clock::duration::period::den};
	encoder_ctx->framerate = AVRational{(int)fps, 1};
	encoder_ctx->sample_aspect_ratio = AVRational{1, 1};
	encoder_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
	encoder_ctx->color_range = AVCOL_RANGE_JPEG;
	encoder_ctx->colorspace = AVCOL_SPC_BT709;
	encoder_ctx->color_trc = AVCOL_TRC_BT709;
	encoder_ctx->color_primaries = AVCOL_PRI_BT709;
	settings.range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
	settings.color_model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
	encoder_ctx->max_b_frames = 0;
	encoder_ctx->bit_rate = settings.bitrate;
	encoder_ctx->gop_size = std::numeric_limits<decltype(encoder_ctx->gop_size)>::max();
	encoder_ctx->hw_frames_ctx = av_buffer_ref(vaapi_frame_ctx.get());

	err = avcodec_open2(encoder_ctx.get(), codec, &opts);
	av_dict_free(&opts);
	if (err < 0)
	{
		throw std::system_error(err, av_error_category(), "Cannot open video encoder codec");
	}

	if (encoder_ctx->delay != 0)
	{
		U_LOG_W("Encoder %d reports a %d frame delay, reprojection will fail", stream_idx, encoder_ctx->delay);
	}

	va_frame = make_av_frame();
	err = av_hwframe_get_buffer(vaapi_frame_ctx.get(), va_frame.get(), 0);
	if (err < 0)
	{
		throw std::system_error(err, av_error_category(), "Cannot create vaapi frame");
	}
	drm_frame = make_av_frame();
	err = av_hwframe_get_buffer(drm_frame_ctx.get(), drm_frame.get(), 0);
	if (err < 0)
	{
		throw std::system_error(err, av_error_category(), "Cannot create vulkan frame");
	}
	av_hwframe_map(va_frame.get(), drm_frame.get(), AV_HWFRAME_MAP_DIRECT);
	va_frame->color_range = AVCOL_RANGE_JPEG;
	va_frame->colorspace = AVCOL_SPC_BT709;
	va_frame->color_primaries = AVCOL_PRI_BT709;
	va_frame->color_trc = AVCOL_TRC_BT709;
	auto desc = (AVDRMFrameDescriptor *)drm_frame->data[0];

	const bool has_modifiers =
	        std::ranges::any_of(vk.device_extensions, [](const char * ext) {
		        return strcmp(ext, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) == 0;
	        });

	assert(desc->nb_layers == desc->nb_objects);

	for (int i = 0; i < desc->nb_layers; ++i)
	{
		std::vector<vk::SubresourceLayout> plane_layouts;
		for (int plane = 0; plane < desc->layers[i].nb_planes; ++plane)
		{
			plane_layouts.push_back({
			        .offset = vk::DeviceSize(desc->layers[i].planes[plane].offset),
			        .rowPitch = vk::DeviceSize(desc->layers[i].planes[plane].pitch),
			});
		}
		vk::ImageDrmFormatModifierExplicitCreateInfoEXT drm_info{
		        .drmFormatModifier = desc->objects[0].format_modifier,
		        .drmFormatModifierPlaneCount = uint32_t(plane_layouts.size()),
		        .pPlaneLayouts = plane_layouts.data(),
		};
		vk::StructureChain image_create_info{
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = drm_to_vulkan_fmt(desc->layers[i].format),
		                .extent = {
		                        .width = uint32_t(drm_frame->width / (i == 0 ? 1 : 2)),
		                        .height = uint32_t(drm_frame->height / (i == 0 ? 1 : 2)),
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .samples = vk::SampleCountFlagBits::e1,
		                .tiling = has_modifiers ? vk::ImageTiling::eDrmFormatModifierEXT : (desc->objects[0].format_modifier == DRM_FORMAT_MOD_LINEAR ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal),
		                .usage = vk::ImageUsageFlagBits::eTransferDst,
		                .sharingMode = vk::SharingMode::eExclusive,
		                .initialLayout = vk::ImageLayout::eUndefined,
		        },
		        vk::ExternalMemoryImageCreateInfo{
		                .pNext = has_modifiers ? &drm_info : nullptr,
		                .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
		        },

		};
		auto & image = (i == 0 ? luma : chroma);
		image = vk.device.createImage(image_create_info.get());

		auto memory_props = vk.device.getMemoryFdPropertiesKHR(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, desc->objects[i].fd);

		vk::StructureChain alloc_info{
		        vk::MemoryAllocateInfo{
		                .allocationSize = desc->objects[i].size,
		                .memoryTypeIndex = vk.get_memory_type(memory_props.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
		        },
		        vk::MemoryDedicatedAllocateInfo{
		                .image = *image,
		        },
		        vk::ImportMemoryFdInfoKHR{
		                .handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
		                .fd = dup(desc->objects[i].fd),
		        },
		};
		try
		{
			mem.push_back(vk.device.allocateMemory(alloc_info.get()));
		}
		catch (...)
		{
			close(alloc_info.get<vk::ImportMemoryFdInfoKHR>().fd);
			throw;
		}
		image.bindMemory(*mem.back(), 0);
	}
}

void video_encoder_va::PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf)
{
	std::array im_barriers = {
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                .image = *luma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .baseArrayLayer = 0,
	                                     .layerCount = 1},
	        },
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                .image = *chroma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .baseArrayLayer = 0,
	                                     .layerCount = 1},
	        },
	};
	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eTransfer,
	        {},
	        nullptr,
	        nullptr,
	        im_barriers);

	cmd_buf.copyImage(
	        src_yuv.luma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *luma,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .srcOffset = {
	                        .x = rect.offset.x,
	                        .y = rect.offset.y,
	                },
	                .dstSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .extent = {
	                        .width = rect.extent.width,
	                        .height = rect.extent.height,
	                        .depth = 1,
	                }});

	cmd_buf.copyImage(
	        src_yuv.chroma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *chroma,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .srcOffset = {
	                        .x = rect.offset.x / 2,
	                        .y = rect.offset.y / 2,
	                },
	                .dstSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .extent = {
	                        .width = rect.extent.width / 2,
	                        .height = rect.extent.height / 2,
	                        .depth = 1,
	                }});
}

void video_encoder_va::PushFrame(bool idr, std::chrono::steady_clock::time_point pts)
{
	va_frame->pict_type = idr ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
	va_frame->pts = pts.time_since_epoch().count();
	int err = avcodec_send_frame(encoder_ctx.get(), va_frame.get());
	if (err)
	{
		throw std::system_error(err, av_error_category(), "avcodec_send_frame failed");
	}
}
