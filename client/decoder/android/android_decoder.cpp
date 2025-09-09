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

#include "android_decoder.h"
#include "application.h"
#include "scenes/stream.h"
#include "utils/named_thread.h"
#include <android/hardware_buffer.h>
#include <cassert>
#include <magic_enum.hpp>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <mutex>
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_raii.hpp>

DEUGLIFY(AMediaFormat)

struct wivrn::android::decoder::mapped_hardware_buffer
{
	vk::raii::DeviceMemory memory = nullptr;
	vk::raii::Image vimage = nullptr;
	vk::raii::ImageView image_view = nullptr;
	vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

namespace
{
const char * mime(wivrn::video_codec codec)
{
	using c = wivrn::video_codec;
	switch (codec)
	{
		case c::h264:
			return "video/avc";
		case c::h265:
			return "video/hevc";
		case c::av1:
			return "video/av01";
	}
	assert(false);
	__builtin_unreachable();
}

void check(media_status_t status, const char * msg)
{
	if (status != AMEDIA_OK)
	{
		spdlog::error("{}: MediaCodec error {}", msg, (int)status);
		throw std::runtime_error("MediaCodec error");
	}
}

struct android_blit_handle : public decoder::blit_handle
{
	std::shared_ptr<wivrn::android::decoder::mapped_hardware_buffer> vk_data;
	std::shared_ptr<AImageReader> image_reader;
	AImage_ptr aimage;
	android_blit_handle(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	        vk::ImageView image_view,
	        vk::Image image,
	        vk::ImageLayout & current_layout,
	        decltype(vk_data) vk_data,
	        decltype(image_reader) image_reader,
	        decltype(aimage) aimage) :
	        decoder::blit_handle(feedback, view_info, image_view, image, current_layout),
	        vk_data(vk_data),
	        image_reader(image_reader),
	        aimage(std::move(aimage)) {}
};

} // namespace

namespace wivrn::android
{

decoder::decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        const wivrn::to_headset::video_stream_description::item & description,
        float fps,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> weak_scene,
        shard_accumulator * accumulator) :
        wivrn::decoder(description), stream_index(stream_index), fps(fps), device(device), weak_scene(weak_scene), accumulator(accumulator)
{
	spdlog::info("hbm_mutex.native_handle() = {}", (void *)hbm_mutex.native_handle());

	AImageReader * ir;
	check(AImageReader_newWithUsage(
	              description.video_width,
	              description.video_height,
	              AIMAGE_FORMAT_PRIVATE,
	              AHARDWAREBUFFER_USAGE_CPU_READ_NEVER | AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
	              scenes::stream::image_buffer_size + 4 /* maxImages */,
	              &ir),
	      "AImageReader_newWithUsage");
	image_reader.reset(ir, AImageReader_deleter{});

	AImageReader_ImageListener listener{this, on_image_available};
	check(AImageReader_setImageListener(ir, &listener), "AImageReader_setImageListener");

	vkGetAndroidHardwareBufferPropertiesANDROID =
	        application::get_vulkan_proc<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
	                "vkGetAndroidHardwareBufferPropertiesANDROID");
	{
		AMediaFormat_ptr format(AMediaFormat_new());
		AMediaFormat_setString(format.get(), AMEDIAFORMAT_KEY_MIME, mime(description.codec));
		// AMediaFormat_setInt32(format.get(), "vendor.qti-ext-dec-low-latency.enable", 1); // Qualcomm low
		// latency mode
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, description.video_width);
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, description.video_height);
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_OPERATING_RATE, std::ceil(fps));
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_PRIORITY, 0);

		media_codec.reset(AMediaCodec_createDecoderByType(mime(description.codec)));

		if (not media_codec)
			throw std::runtime_error(std::string("Cannot create decoder for MIME type ") + mime(description.codec));

		char * codec_name;
		check(AMediaCodec_getName(media_codec.get(), &codec_name), "AMediaCodec_getName");
		spdlog::info("Created MediaCodec decoder \"{}\"", codec_name);
		AMediaCodec_releaseName(media_codec.get(), codec_name);

		ANativeWindow * window;

		check(AImageReader_getWindow(image_reader.get(), &window), "AImageReader_getWindow");

		AMediaCodecOnAsyncNotifyCallback callback{
		        .onAsyncInputAvailable = decoder::on_media_input_available,
		        .onAsyncOutputAvailable = decoder::on_media_output_available,
		        .onAsyncFormatChanged = decoder::on_media_format_changed,
		        .onAsyncError = decoder::on_media_error,
		};
		check(AMediaCodec_setAsyncNotifyCallback(media_codec.get(), callback, this),
		      "AMediaCodec_setAsyncNotifyCallback");

		check(AMediaCodec_configure(media_codec.get(), format.get(), window, nullptr /* crypto */, 0 /* flags */),
		      "AMediaCodec_configure");

		check(AMediaCodec_start(media_codec.get()), "AMediaCodec_start");
	}
	worker = utils::named_thread(
	        "decoder-" + std::to_string(stream_index),
	        [this]() {
		        while (true)
		        {
			        try
			        {
				        if (jobs.pop()())
					        return;
			        }
			        catch (const utils::sync_queue_closed & e)
			        {
				        return;
			        }
			        catch (const std::exception & e)
			        {
				        spdlog::error("error in decoder thread: {}", e.what());
			        }
		        }
	        });
}

decoder::~decoder()
{
	if (media_codec)
	{
		AMediaCodec_stop(media_codec.get());
		jobs.push([]() { return true; });
		if (worker.joinable())
			worker.join();
	}
	input_buffers.close();
	jobs.close();

	if (worker.joinable())
		worker.join();

	spdlog::info("decoder::~decoder");
}

void decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	if (current_input_buffer.data == nullptr)
		current_input_buffer = input_buffers.pop();
	else if (current_input_buffer.frame_index != frame_index)
	{
		// Reuse the input buffer, discard existing data
		current_input_buffer.data_size = 0;
	}
	current_input_buffer.frame_index = frame_index;

	for (const auto & sub_data: data)
	{
		if (current_input_buffer.data_size + sub_data.size() > current_input_buffer.capacity)
		{
			spdlog::error("data to decode is larger than decoder buffer, skipping frame");
			return;
		}

		memcpy(current_input_buffer.data + current_input_buffer.data_size, sub_data.data(), sub_data.size());
		current_input_buffer.data_size += sub_data.size();
	}

	if (partial)
		return;

	jobs.push([=, idx = current_input_buffer.idx, data_size = current_input_buffer.data_size, mc = media_codec.get()]() {
		uint64_t timestamp = frame_index * 10'000;
		auto status = AMediaCodec_queueInputBuffer(mc, idx, 0, data_size, timestamp, 0);
		if (status != AMEDIA_OK)
			spdlog::error("AMediaCodec_queueInputBuffer: MediaCodec error {}({})",
			              int(status),
			              std::string(magic_enum::enum_name(status)).c_str());
		return false;
	});
	current_input_buffer = input_buffer{};
}

void decoder::frame_completed(const wivrn::from_headset::feedback & feedback, const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
	if (not media_codec)
	{
		// If media_codec is not initialized, frame processing ends here
		if (auto scene = weak_scene.lock())
			scene->send_feedback(feedback);
	}

	// nothing required for decoder, mediacodec will callback when done
	frame_infos.push(frame_info{
	        .feedback = feedback,
	        .view_info = view_info,
	});
}

void decoder::on_image_available(void * context, AImageReader * reader)
{
	try
	{
		static_cast<decoder *>(context)->on_image_available(reader);
	}
	catch (std::exception & e)
	{
		spdlog::error("Exception in decoder::on_image_available: {}", e.what());
	}
}

void decoder::on_image_available(AImageReader * reader)
{
	assert(reader == image_reader.get());
	// Executed on image reader thread

	decltype(android_blit_handle::aimage) image;
	try
	{
		AImage * tmp;
		check(AImageReader_acquireLatestImage(image_reader.get(), &tmp), "AImageReader_acquireLatestImage");
		image.reset(tmp);

		int64_t fake_timestamp_ns;
		check(AImage_getTimestamp(image.get(), &fake_timestamp_ns), "AImage_getTimestamp");
		uint64_t frame_index = (fake_timestamp_ns + 5'000'000) / (10'000'000);

		frame_infos.drop_until([frame_index](auto & x) { return x.feedback.frame_index >= frame_index; });

		auto info = frame_infos.pop_if([frame_index](auto & x) { return x.feedback.frame_index == frame_index; });

		if (!info)
		{
			spdlog::warn("No frame info for frame {}, dropping frame", frame_index);
			return;
		}

		assert(info->feedback.frame_index == frame_index);

		auto vk_data = map_hardware_buffer(image.get());

		auto handle = std::make_shared<android_blit_handle>(
		        info->feedback,
		        info->view_info,
		        *vk_data->image_view,
		        *vk_data->vimage,
		        vk_data->layout,
		        std::move(vk_data),
		        image_reader,
		        std::move(image));

		if (auto scene = weak_scene.lock())
			scene->push_blit_handle(accumulator, std::move(handle));
	}
	catch (...)
	{
	}
}

void decoder::create_sampler(const AHardwareBuffer_Desc & buffer_desc, vk::AndroidHardwareBufferFormatPropertiesANDROID & ahb_format)
{
	assert(ahb_format.externalFormat != 0);
	spdlog::info("AndroidHardwareBufferProperties");
	spdlog::info("  Vulkan format: {}", vk::to_string(ahb_format.format));
	spdlog::info("  External format: {:#x}", ahb_format.externalFormat);
	spdlog::info("  Format features: {}", vk::to_string(ahb_format.formatFeatures));
	spdlog::info("  samplerYcbcrConversionComponents: ({}, {}, {}, {})",
	             vk::to_string(ahb_format.samplerYcbcrConversionComponents.r),
	             vk::to_string(ahb_format.samplerYcbcrConversionComponents.g),
	             vk::to_string(ahb_format.samplerYcbcrConversionComponents.b),
	             vk::to_string(ahb_format.samplerYcbcrConversionComponents.a));
	spdlog::info("  Suggested YCbCr model: {}", vk::to_string(ahb_format.suggestedYcbcrModel));
	spdlog::info("  Suggested YCbCr range: {}", vk::to_string(ahb_format.suggestedYcbcrRange));
	spdlog::info("  Suggested X chroma offset: {}", vk::to_string(ahb_format.suggestedXChromaOffset));
	spdlog::info("  Suggested Y chroma offset: {}", vk::to_string(ahb_format.suggestedYChromaOffset));

	vk::Filter yuv_filter;
	if (ahb_format.formatFeatures & vk::FormatFeatureFlagBits::eSampledImageYcbcrConversionLinearFilter)
		yuv_filter = vk::Filter::eLinear;
	else
		yuv_filter = vk::Filter::eNearest;

	// Create VkSamplerYcbcrConversion
	vk::StructureChain ycbcr_create_info{
	        vk::SamplerYcbcrConversionCreateInfo{
	                .format = vk::Format::eUndefined,
	                .ycbcrModel = ahb_format.suggestedYcbcrModel,
	                .ycbcrRange = ahb_format.suggestedYcbcrRange,
	                .components = ahb_format.samplerYcbcrConversionComponents,
	                .xChromaOffset = ahb_format.suggestedXChromaOffset,
	                .yChromaOffset = ahb_format.suggestedYChromaOffset,
	                .chromaFilter = yuv_filter,
	        },
	        vk::ExternalFormatANDROID{
	                .externalFormat = ahb_format.externalFormat,
	        },
	};

	// suggested values from decoder don't actually read the metadata, so it's garbage
	if (description.range)
		ycbcr_create_info.get<vk::SamplerYcbcrConversionCreateInfo>().ycbcrRange = vk::SamplerYcbcrRange(*description.range);

	if (description.color_model)
		ycbcr_create_info.get<vk::SamplerYcbcrConversionCreateInfo>().ycbcrModel = vk::SamplerYcbcrModelConversion(*description.color_model);

	ycbcr_conversion = vk::raii::SamplerYcbcrConversion(device, ycbcr_create_info.get<vk::SamplerYcbcrConversionCreateInfo>());

	// Create VkSampler
	vk::StructureChain sampler_info{
	        vk::SamplerCreateInfo{
	                .magFilter = yuv_filter,
	                .minFilter = yuv_filter,
	                .mipmapMode = vk::SamplerMipmapMode::eNearest,
	                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
	                .mipLodBias = 0.0f,
	                .anisotropyEnable = VK_FALSE,
	                .maxAnisotropy = 1,
	                .compareEnable = VK_FALSE,
	                .compareOp = vk::CompareOp::eNever,
	                .minLod = 0.0f,
	                .maxLod = 0.0f,
	                .borderColor = vk::BorderColor::eFloatOpaqueWhite, // TODO TBC
	                .unnormalizedCoordinates = VK_FALSE,
	        },
	        vk::SamplerYcbcrConversionInfo{
	                .conversion = *ycbcr_conversion,
	        },
	};

	ycbcr_sampler = vk::raii::Sampler(device, sampler_info.get());
}

std::shared_ptr<decoder::mapped_hardware_buffer> decoder::map_hardware_buffer(AImage * image)
{
	AHardwareBuffer * hardware_buffer;
	check(AImage_getHardwareBuffer(image, &hardware_buffer), "AImage_getHardwareBuffer");

	std::unique_lock lock(hbm_mutex);

	AHardwareBuffer_Desc buffer_desc{};
	AHardwareBuffer_describe(hardware_buffer, &buffer_desc);

	auto [properties, format_properties] = device.getAndroidHardwareBufferPropertiesANDROID<vk::AndroidHardwareBufferPropertiesANDROID, vk::AndroidHardwareBufferFormatPropertiesANDROID>(*hardware_buffer);

	if (!*ycbcr_sampler || memcmp(&ahb_format, &format_properties, sizeof(format_properties)))
	{
		memcpy(&ahb_format, &format_properties, sizeof(format_properties));
		extent_ = {buffer_desc.width, buffer_desc.height};
		spdlog::info("decoded image size: {}x{}", buffer_desc.width, buffer_desc.height);
		create_sampler(buffer_desc, ahb_format);
		hardware_buffer_map.clear();
		// TODO tell the reprojector to recreate the pipeline
	}

	auto it = hardware_buffer_map.find(hardware_buffer);
	if (it != hardware_buffer_map.end())
		return it->second;

	vk::StructureChain img_info{
	        vk::ImageCreateInfo{
	                .flags = {},
	                .imageType = vk::ImageType::e2D,
	                .format = vk::Format::eUndefined,
	                .extent = {buffer_desc.width, buffer_desc.height, 1},
	                .mipLevels = 1,
	                .arrayLayers = 1,
	                .samples = vk::SampleCountFlagBits::e1,
	                .tiling = vk::ImageTiling::eOptimal,
	                .usage = vk::ImageUsageFlagBits::eSampled,
	                .sharingMode = vk::SharingMode::eExclusive,
	                .initialLayout = vk::ImageLayout::eUndefined,
	        },
	        vk::ExternalMemoryImageCreateInfo{
	                .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID,
	        },
	        vk::ExternalFormatANDROID{
	                .externalFormat = format_properties.externalFormat,
	        },
	};

	vk::raii::Image vimage(device, img_info.get());

	assert(properties.memoryTypeBits != 0);
	vk::StructureChain mem_info{
	        vk::MemoryAllocateInfo{
	                .allocationSize = properties.allocationSize,
	                .memoryTypeIndex = (uint32_t)(ffs(properties.memoryTypeBits) - 1),
	        },
	        vk::MemoryDedicatedAllocateInfo{
	                .image = *vimage,
	        },
	        vk::ImportAndroidHardwareBufferInfoANDROID{
	                .buffer = hardware_buffer,
	        },
	};

	vk::raii::DeviceMemory memory(device, mem_info.get());

	vimage.bindMemory(*memory, 0);

	vk::StructureChain iv_info{
	        vk::ImageViewCreateInfo{
	                .image = *vimage,
	                .viewType = vk::ImageViewType::e2D,
	                .format = vk::Format::eUndefined,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        },
	        vk::SamplerYcbcrConversionInfo{
	                .conversion = *ycbcr_conversion,
	        },
	};

	application::ignore_debug_reports_for(*vimage);
	vk::raii::ImageView image_view(device, iv_info.get());
	application::unignore_debug_reports_for(*vimage);

	auto handle = std::make_shared<mapped_hardware_buffer>();
	handle->vimage = std::move(vimage);
	handle->image_view = std::move(image_view);
	handle->memory = std::move(memory);

	hardware_buffer_map[hardware_buffer] = handle;
	return handle;
}

void decoder::on_media_error(AMediaCodec *, void * userdata, media_status_t error, int32_t actionCode, const char * detail)
{
	spdlog::warn("Mediacodec error: {}", detail);

	if (error == AMEDIA_ERROR_MALFORMED)
	{
		// Send an empty feedback packet, encoder will know we are lost
		auto self = (decoder *)userdata;
		if (auto scene = self->weak_scene.lock())
			scene->send_feedback(
			        wivrn::from_headset::feedback{
			                .stream_index = self->stream_index});
	}
}
void decoder::on_media_format_changed(AMediaCodec *, void * userdata, AMediaFormat *)
{
	spdlog::info("Mediacodec format changed");
}
void decoder::on_media_input_available(AMediaCodec * media_codec, void * userdata, int32_t index)
{
	auto self = (decoder *)userdata;
	size_t size;
	uint8_t * buffer = AMediaCodec_getInputBuffer(media_codec, index, &size);
	self->input_buffers.push({
	        .idx = index,
	        .capacity = size,
	        .data = buffer,
	});
}
void decoder::on_media_output_available(AMediaCodec * media_codec, void * userdata, int32_t index, AMediaCodecBufferInfo * bufferInfo)
{
	auto self = (decoder *)userdata;
	self->jobs.push([=]() {
		auto status = AMediaCodec_releaseOutputBuffer(media_codec, index, true);
		// will trigger on_image_available through ImageReader
		if (status != AMEDIA_OK)
			spdlog::error("AMediaCodec_releaseOutputBuffer: MediaCodec error {}({})",
			              int(status),
			              std::string(magic_enum::enum_name(status)).c_str());
		return false;
	});
}

static bool hardware_accelerated(AMediaCodec * media_codec)
{
	// MediaCodecInfo has isHardwareAccelerated, but this does not exist in NDK.
	char * name;
	AMediaCodec_getName(media_codec, &name);
	auto release = [&]() {
		AMediaCodec_releaseName(media_codec, name);
	};
	for (const char * prefix: {
	             "OMX.google",
	             "c2.android",
	     })
	{
		if (std::string_view(name).starts_with(prefix))
		{
			release();
			return false;
		}
	}
	release();
	return true;
}

void decoder::supported_codecs(std::vector<wivrn::video_codec> & result)
{
	// Make sure we update this code when codecs are changed
	static_assert(magic_enum::enum_count<wivrn::video_codec>() == 3);

	// In order or preference, from preferred to least preferred
	for (auto codec: {
	             wivrn::video_codec::av1,
	             wivrn::video_codec::h264,
	             wivrn::video_codec::h265,
	     })
	{
		AMediaCodec_ptr media_codec(AMediaCodec_createDecoderByType(mime(codec)));

		bool supported = media_codec and hardware_accelerated(media_codec.get());
		if (supported)
			result.push_back(codec);

		spdlog::info("video codec {}: {}supported", magic_enum::enum_name(codec), supported ? "" : "NOT ");
	}
}

} // namespace wivrn::android
