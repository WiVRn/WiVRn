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
#include "vk/vk_helpers.h"

#include "video_encoder_va.h"

#include "encoder/encoder_settings.h"

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
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace wivrn
{

namespace
{
const char * encoder(video_codec codec)
{
	switch (codec)
	{
		case video_codec::h264:
			return "h264_vaapi";
		case video_codec::h265:
			return "hevc_vaapi";
		case video_codec::av1:
			return "av1_vaapi";
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
	return hw_frames_ref;
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

av_buffer_ptr make_drm_hw_ctx(vk::raii::PhysicalDevice & physical_device, const std::optional<std::string> & device)
{
	const auto render_device = device ? *device : get_render_device(physical_device);
	AVBufferRef * hw_ctx;
	int err = av_hwdevice_ctx_create(&hw_ctx, AV_HWDEVICE_TYPE_DRM, render_device ? render_device->c_str() : NULL, NULL, 0);
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

video_encoder_va::video_encoder_va(wivrn_vk_bundle & vk,
                                   wivrn::encoder_settings & settings,
                                   float fps,
                                   uint8_t stream_idx) :
        video_encoder_ffmpeg(stream_idx, settings.channels, settings.bitrate_multiplier),
        synchronization2(vk.vk.features.synchronization_2)
{
	auto drm_hw_ctx = make_drm_hw_ctx(vk.physical_device, settings.device);
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
	switch (settings.codec)
	{
		case video_codec::h264:
			encoder_ctx->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
			av_dict_set(&opts, "coder", "cavlc", 0);
			av_dict_set(&opts, "rc_mode", "CBR", 0);
			break;
		case video_codec::h265:
			encoder_ctx->profile = FF_PROFILE_HEVC_MAIN;
			break;
		case video_codec::av1:
			encoder_ctx->profile = FF_PROFILE_AV1_MAIN;
			break;
	}
	for (auto option: settings.options)
	{
		av_dict_set(&opts, option.first.c_str(), option.second.c_str(), 0);
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

	const bool has_modifiers =
	        std::ranges::any_of(vk.device_extensions, [](const char * ext) {
		        return strcmp(ext, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) == 0;
	        });
	for (auto & slot: in)
	{
		auto & va_frame = slot.va_frame;
		auto & drm_frame = slot.drm_frame;
		auto & luma = slot.luma;
		auto & chroma = slot.chroma;
		auto & mem = slot.mem;
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

		// layer == image
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
			                .tiling = has_modifiers ? vk::ImageTiling::eDrmFormatModifierEXT : ((desc->objects[0].format_modifier == DRM_FORMAT_MOD_LINEAR || desc->objects[0].format_modifier == DRM_FORMAT_MOD_INVALID) ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal),
			                .usage = vk::ImageUsageFlagBits::eTransferDst,
			                .sharingMode = vk::SharingMode::eExclusive,
			                .initialLayout = vk::ImageLayout::eUndefined,
			        },
			        vk::ExternalMemoryImageCreateInfo{
			                .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
			        },
			        vk::ImageDrmFormatModifierExplicitCreateInfoEXT{
			                .drmFormatModifier = desc->objects[0].format_modifier,
			                .drmFormatModifierPlaneCount = uint32_t(plane_layouts.size()),
			                .pPlaneLayouts = plane_layouts.data(),
			        },
			};

			if (not has_modifiers)
				image_create_info.unlink<vk::ImageDrmFormatModifierExplicitCreateInfoEXT>();

			auto & image = (i == 0 ? luma : chroma);
			image = vk.device.createImage(image_create_info.get());
			vk.name(image, i == 0 ? "va encoder luma image" : "va encoder chroma image");

			auto [req, ded_req] = vk.device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>({.image = *image});

			const auto & object = desc->objects[desc->layers[i].planes[0].object_index];
			auto memory_props = vk.device.getMemoryFdPropertiesKHR(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, object.fd);

			vk::StructureChain alloc_info{
			        vk::MemoryAllocateInfo{
			                .allocationSize = req.memoryRequirements.size,
			                .memoryTypeIndex = vk.get_memory_type(memory_props.memoryTypeBits, {}),
			        },
			        vk::ImportMemoryFdInfoKHR{
			                .handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
			                .fd = dup(object.fd),
			        },
			        vk::MemoryDedicatedAllocateInfo{
			                .image = *image,
			        },
			};
			if (not(ded_req.prefersDedicatedAllocation or ded_req.requiresDedicatedAllocation))
				alloc_info.unlink<vk::MemoryDedicatedAllocateInfo>();

			try
			{
				mem.emplace_back(vk.device, alloc_info.get());
				vk.name(mem.back(), "va encoder memory");
			}
			catch (...)
			{
				close(alloc_info.get<vk::ImportMemoryFdInfoKHR>().fd);
				throw;
			}
		}

		{
			std::vector<vk::BindImageMemoryInfo> bind_info;
			std::vector<vk::BindImagePlaneMemoryInfo> plane_info;
			plane_info.reserve(8); // at most 8 elements in ffmpeg data
			for (int i = 0; i < desc->nb_layers; i++)
			{
				const int planes = desc->layers[i].nb_planes;
				const int signal_p = has_modifiers && (planes > 1);
				for (int j = 0; j < planes; j++)
				{
					const std::array aspect{
					        vk::ImageAspectFlagBits::ePlane0,
					        vk::ImageAspectFlagBits::ePlane1,
					        vk::ImageAspectFlagBits::ePlane2,
					};
					plane_info.push_back({.planeAspect = aspect.at(j)});

					bind_info.push_back(
					        {
					                .pNext = signal_p ? &plane_info.back() : nullptr,
					                .image = *((i == 0) ? luma : chroma),
					                .memory = *mem[i],
					                .memoryOffset = has_modifiers ? 0 : (vk::DeviceSize)desc->layers[i].planes[j].offset,
					        });
				}
			}
			vk.device.bindImageMemory2(bind_info);
		}
	}
}

std::pair<bool, vk::Semaphore> video_encoder_va::present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index)
{
	std::array im_barriers = {
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                .image = *in[slot].luma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .layerCount = 1},
	        },
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                .image = *in[slot].chroma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
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
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *in[slot].luma,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .baseArrayLayer = uint32_t(channels),
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
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *in[slot].chroma,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .baseArrayLayer = uint32_t(channels),
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

	for (auto & b: im_barriers)
	{
		b.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
		b.dstAccessMask = vk::AccessFlagBits::eNone;
		b.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		b.newLayout = vk::ImageLayout::eGeneral;
	}

	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTransfer,
	        synchronization2 ? vk::PipelineStageFlagBits::eNone : vk::PipelineStageFlagBits::eAllCommands,
	        {},
	        nullptr,
	        nullptr,
	        im_barriers);
	return {false, nullptr};
}

void video_encoder_va::push_frame(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot)
{
	auto & va_frame = in[slot].va_frame;
	va_frame->pict_type = idr ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
	va_frame->pts = pts.time_since_epoch().count();
	int err = avcodec_send_frame(encoder_ctx.get(), va_frame.get());
	if (err)
	{
		throw std::system_error(err, av_error_category(), "avcodec_send_frame failed");
	}
}
} // namespace wivrn
