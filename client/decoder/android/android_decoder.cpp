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
#include "vk/shader.h"
#include "vk/pipeline.h"
#include <algorithm>
#include <android/hardware_buffer.h>
#include <cassert>
#include <chrono>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_raii.hpp>

DEUGLIFY(AMediaFormat)

struct wivrn::android::decoder::pipeline_context
{
	vk::raii::Device& device;
	vk::AndroidHardwareBufferFormatPropertiesANDROID ahb_format;

	vk::raii::SamplerYcbcrConversion ycbcr_conversion = nullptr;
	vk::raii::Sampler sampler = nullptr;

	vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
	vk::raii::DescriptorPool descriptor_pool = nullptr;
	std::mutex descriptor_pool_mutex;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::Pipeline pipeline = nullptr;

	pipeline_context(vk::raii::Device& device, const AHardwareBuffer_Desc& buffer_desc, vk::AndroidHardwareBufferFormatPropertiesANDROID & ahb_format, vk::raii::RenderPass& renderpass, const to_headset::video_stream_description::item& description) :
	        device(device), ahb_format(ahb_format)
	{
		spdlog::info("descriptor_pool_mutex.native_handle() = {}", (void*)descriptor_pool_mutex.native_handle());

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
		vk::StructureChain ycbcr_create_info
		{
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
			}
		};


		sampler = vk::raii::Sampler(device, sampler_info.get<vk::SamplerCreateInfo>());


		// Create VkDescriptorSetLayout with an immutable sampler
		vk::DescriptorSetLayoutBinding sampler_layout_binding{
			.binding = 0,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = 1,
			.stageFlags = vk::ShaderStageFlagBits::eFragment,
		};
		sampler_layout_binding.setImmutableSamplers(*sampler);

		vk::DescriptorSetLayoutCreateInfo layout_info;
		layout_info.setBindings(sampler_layout_binding);

		descriptor_set_layout = vk::raii::DescriptorSetLayout(device, layout_info);


		vk::DescriptorPoolSize pool_size{
			.type = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = 100,
		};

		vk::DescriptorPoolCreateInfo pool_info{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = pool_size.descriptorCount,
		};
		pool_info.setPoolSizes(pool_size);

		descriptor_pool = vk::raii::DescriptorPool(device, pool_info);

		std::array useful_size =
		{
			float(description.width) / buffer_desc.width,
			float(description.height) / buffer_desc.height
		};
		spdlog::info("useful size: {}x{} with buffer {}x{}",
				description.width, description.height,
				buffer_desc.width, buffer_desc.height);

		std::array specialization_constants_desc{
			vk::SpecializationMapEntry{
				.constantID = 0,
				.offset = 0,
				.size = sizeof(float),
			},
			vk::SpecializationMapEntry{
				.constantID = 1,
				.offset = sizeof(float),
				.size = sizeof(float),
			}
		};

		vk::SpecializationInfo specialization_info;
		specialization_info.setMapEntries(specialization_constants_desc);
		specialization_info.setData<float>(useful_size);

		// Create graphics pipeline
		vk::raii::ShaderModule vertex_shader = load_shader(device, "stream.vert");
		vk::raii::ShaderModule fragment_shader = load_shader(device, "stream.frag");


		vk::PipelineLayoutCreateInfo pipeline_layout_info;
		pipeline_layout_info.setSetLayouts(*descriptor_set_layout);

		layout = vk::raii::PipelineLayout(device, pipeline_layout_info);

		vk::pipeline_builder pipeline_info
		{
			.flags = {},
			.Stages = {{
				.stage = vk::ShaderStageFlagBits::eVertex,
				.module = *vertex_shader,
				.pName = "main",
				.pSpecializationInfo = &specialization_info,
			},{
				.stage = vk::ShaderStageFlagBits::eFragment,
				.module = *fragment_shader,
				.pName = "main",
			}},
			.VertexInputState = {.flags = {}},
			.VertexBindingDescriptions = {},
			.VertexAttributeDescriptions = {},
			.InputAssemblyState = {{
				.topology = vk::PrimitiveTopology::eTriangleStrip,
			}},
			.ViewportState = {.flags = {}},
			.RasterizationState = {{
				.polygonMode = vk::PolygonMode::eFill,
				.lineWidth = 1,
			}},
			.MultisampleState = {{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
			}},
			.ColorBlendState = {.flags = {}},
			.ColorBlendAttachments = {{
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
			}},
			.DynamicState = {.flags={}},
			.DynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
			.layout = *layout,
			.renderPass = *renderpass,
			.subpass = 0,
		};

		pipeline = vk::raii::Pipeline(device, application::get_pipeline_cache(), pipeline_info);
	}
};

struct wivrn::android::decoder::mapped_hardware_buffer
{
	std::shared_ptr<pipeline_context> pipeline;
	vk::raii::DeviceMemory memory = nullptr;
	vk::raii::Image vimage = nullptr;
	vk::raii::ImageView image_view = nullptr;
	vk::raii::DescriptorSet descriptor_set = nullptr;
	vk::ImageLayout layout = vk::ImageLayout::eUndefined;

	~mapped_hardware_buffer()
	{
		assert(pipeline);
		{
			std::unique_lock lock(pipeline->descriptor_pool_mutex);
			descriptor_set = nullptr;
		}
	}
};

namespace
{
const char * mime(xrt::drivers::wivrn::video_codec codec)
{
	using c = xrt::drivers::wivrn::video_codec;
	switch (codec)
	{
		case c::h264:
			return "video/avc";
		case c::h265:
			return "video/hevc";
	}
	assert(false);
}

void check(media_status_t status, const char * msg)
{
	if (status != AMEDIA_OK)
	{
		spdlog::error("{}: MediaCodec error {}", msg, (int)status);
		throw std::runtime_error("MediaCodec error");
	}
}

namespace nal_h264
{
static const int sps = 7;
static const int pps = 8;
} // namespace nal_h264

namespace nal_h265
{
static const int vps = 32;
static const int sps = 33;
static const int pps = 34;
static const int aud = 35;
static const int filler = 38;
}; // namespace nal_h265

enum class nal_class
{
	csd,
	data,
	garbage
};

nal_class get_nal_class_h264(uint8_t * nal)
{
	uint8_t nal_type = (nal[2] == 0 ? nal[4] : nal[3]) & 0x1F;
	// spdlog::info("H264 NAL type {}", (int)nal_type);
	switch (nal_type)
	{
		case nal_h264::sps:
		case nal_h264::pps:
			return nal_class::csd;
		default:
			return nal_class::data;
	}
}

nal_class get_nal_class_h265(uint8_t * nal)
{
	uint8_t nal_type = ((nal[2] == 0 ? nal[4] : nal[3]) >> 1) & 0x3F;
	// spdlog::info("H265 NAL type {}", (int)nal_type);
	switch (nal_type)
	{
		case nal_h265::vps:
		case nal_h265::sps:
		case nal_h265::pps:
			return nal_class::csd;
		case nal_h265::aud:
		case nal_h265::filler:
			return nal_class::garbage;
		default:
			return nal_class::data;
	}
}

nal_class get_nal_class(uint8_t * nal, xrt::drivers::wivrn::video_codec codec)
{
	switch (codec)
	{
		case xrt::drivers::wivrn::video_codec::h264:
			return get_nal_class_h264(nal);

		case xrt::drivers::wivrn::video_codec::h265:
			return get_nal_class_h265(nal);
	}
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> filter_csd(std::span<uint8_t> packet,
                                                                 xrt::drivers::wivrn::video_codec codec)
{
	if (packet.size() < 4)
		return {};

	std::array<uint8_t, 3> header = {{0, 0, 1}};
	auto end = packet.end();
	auto header_start = packet.begin();

	std::pair<std::vector<uint8_t>, std::vector<uint8_t>> out;

	while (header_start != end)
	{
		auto next_header = std::search(header_start + 3, end, header.begin(), header.end());
		if (next_header != end and next_header[-1] == 0)
		{
			next_header--;
		}

		switch (get_nal_class(&*header_start, codec))
		{
			case nal_class::csd:
				out.first.insert(out.first.end(), header_start, next_header);
				break;
			case nal_class::data:
				out.second.insert(out.second.end(), header_start, next_header);
				break;
			case nal_class::garbage:
				break;
		}

		header_start = next_header;
	}

	return out;
}
} // namespace

namespace wivrn::android
{

void decoder::push_nals(std::span<std::span<const uint8_t>> data, int64_t timestamp, uint32_t flags)
{
	auto t1 = application::now();
	auto input_buffer = input_buffers.pop();
	auto t2 = application::now();
	assert(input_buffer >= 0);

	if (t2 - t1 > 1'000'000)
		spdlog::warn("input_buffers.pop() took {}µs", (t2 - t1) / 1000);

	size_t size;
	t1 = application::now();
	uint8_t * buffer = AMediaCodec_getInputBuffer(media_codec.get(), input_buffer, &size);
	t2 = application::now();
	if (t2 - t1 > 1'000'000)
		spdlog::warn("AMediaCodec_getInputBuffer() took {}µs", (t2 - t1) / 1000);
	size_t data_size = 0;

	for (const auto & sub_data: data)
	{
		if (sub_data.size() > size)
		{
			spdlog::error("data to decode is larger than decoder buffer, skipping frame");
			return;
		}

		memcpy(buffer + data_size, sub_data.data(), sub_data.size());
		data_size += sub_data.size();
	}

	t1 = application::now();
	check(AMediaCodec_queueInputBuffer(media_codec.get(), input_buffer, 0 /* offset */, data_size, timestamp, flags),
	      "AMediaCodec_queueInputBuffer");
	t2 = application::now();
	if (t2 - t1 > 1'000'000)
		spdlog::warn("AMediaCodec_queueInputBuffer() took {}µs", (t2 - t1) / 1000);
}

decoder::decoder(
        vk::raii::Device& device,
        vk::raii::PhysicalDevice& physical_device,
        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
        float fps,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> weak_scene,
        shard_accumulator * accumulator) :
        description(description), fps(fps), device(device), weak_scene(weak_scene), accumulator(accumulator)
{
	spdlog::info("hbm_mutex.native_handle() = {}", (void*)hbm_mutex.native_handle());

	AImageReader * ir;
	check(AImageReader_newWithUsage(description.width, description.height, AIMAGE_FORMAT_PRIVATE, AHARDWAREBUFFER_USAGE_CPU_READ_NEVER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 5 /* maxImages */, &ir),
	      "AImageReader_newWithUsage");
	image_reader.reset(ir);

	AImageReader_ImageListener listener{this, on_image_available};
	check(AImageReader_setImageListener(ir, &listener), "AImageReader_setImageListener");

	vkGetAndroidHardwareBufferPropertiesANDROID =
	        application::get_vulkan_proc<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
	                "vkGetAndroidHardwareBufferPropertiesANDROID");

	output_releaser = utils::named_thread("decoder-" + std::to_string(stream_index), [this]() {
		while (true)
		{
			try
			{
				auto index = output_buffers.pop();
				auto status = AMediaCodec_releaseOutputBuffer(media_codec.get(), index, true);
				// will trigger on_image_available through ImageReader
				if (status != AMEDIA_OK)
					spdlog::error("AMediaCodec_releaseOutputBuffer: MediaCodec error {}",
					              (int)status);
			}
			catch (const utils::sync_queue_closed & e)
			{
				return;
			}
			catch (const std::exception & e)
			{
				spdlog::error("error in output releaser thread: {}", e.what());
			}
		}
	});
}

decoder::~decoder()
{
	input_buffers.close();
	output_buffers.close();

	if (output_releaser.joinable())
		output_releaser.join();

	spdlog::info("decoder::~decoder");
}

void decoder::set_blit_targets(std::vector<decoder::blit_target> targets, vk::Format format)
{
	// Create renderpass
	vk::AttachmentDescription color_desc{
		.format = format,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eLoad,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentReference color_ref{
		.attachment = 0,
		.layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::SubpassDescription subpass{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	};
	subpass.setColorAttachments(color_ref);

	vk::RenderPassCreateInfo renderpass_info{
		.flags = {},

	};
	renderpass_info.setAttachments(color_desc);
	renderpass_info.setSubpasses(subpass);

	renderpass = vk::raii::RenderPass(device, renderpass_info);

	blit_targets = std::move(targets);

	for (auto & i: blit_targets)
	{
		vk::FramebufferCreateInfo fb_create_info{
			.renderPass = *renderpass,
			.attachmentCount = 1,
			.pAttachments = &i.image_view,
			.width = i.extent.width,
			.height = i.extent.height,
			.layers = 1
		};

		i.framebuffer = std::make_shared<vk::raii::Framebuffer>(device, fb_create_info);
	}
}

void decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	if (!media_codec)
	{
		std::vector<uint8_t> contiguous_data;
		for (const auto& d: data)
			contiguous_data.insert(contiguous_data.end(), d.begin(), d.end());
		auto [csd, not_csd] = filter_csd(contiguous_data, description.codec);
		if (csd.empty())
		{
			// We can't initizale the encoder, drop the data
			return;
		}

		AMediaFormat_ptr format(AMediaFormat_new());
		AMediaFormat_setString(format.get(), AMEDIAFORMAT_KEY_MIME, mime(description.codec));
		// AMediaFormat_setInt32(format.get(), "vendor.qti-ext-dec-low-latency.enable", 1); // Qualcomm low
		// latency mode
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, description.width);
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, description.height);
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_OPERATING_RATE, std::ceil(fps));
		AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_PRIORITY, 0);
		//  AMediaFormat_setBuffer(format.get(), AMEDIAFORMAT_KEY_CSD_0, csd.data(), csd.size());

		media_codec.reset(AMediaCodec_createDecoderByType(mime(description.codec)));

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

	uint64_t fake_timestamp_us = frame_index * 10'000;
	push_nals(data, fake_timestamp_us, partial ? AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME : 0);
}

void decoder::frame_completed(xrt::drivers::wivrn::from_headset::feedback & feedback, const xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
	if (not media_codec)
	{
		// If media_codec is not initialized, frame processing ends here
		auto scene = weak_scene.lock();
		if (scene)
		{
			scene->send_feedback(feedback);
		}
	}

	// nothing required for decoder, mediacodec will callback when done
	feedback.sent_to_decoder = application::now();
	frame_infos.push(std::make_pair(feedback, view_info));
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

	AImage * image = nullptr;
	try
	{
		check(AImageReader_acquireLatestImage(image_reader.get(), &image), "AImageReader_acquireLatestImage");

		int64_t fake_timestamp_ns;
		check(AImage_getTimestamp(image, &fake_timestamp_ns), "AImage_getTimestamp");
		uint64_t frame_index = (fake_timestamp_ns + 5'000'000) / (10'000'000);

		frame_infos.drop_until([frame_index](auto & x) { return x.first.frame_index >= frame_index; });

		auto info = frame_infos.pop_if([frame_index](auto & x) { return x.first.frame_index == frame_index; });

		if (!info)
		{
			spdlog::warn("No frame info for frame {}, dropping frame", frame_index);
			AImage_delete(image);
			return;
		}

		auto [feedback, view_info] = *info;

		feedback.received_from_decoder = application::now();
		assert(feedback.frame_index == frame_index);

		auto handle = std::make_shared<decoder::blit_handle>();
		handle->feedback = feedback;
		handle->view_info = view_info;
		handle->vk_data = map_hardware_buffer(image);
		handle->aimage = image;

		if (auto scene = weak_scene.lock())
			scene->push_blit_handle(accumulator, std::move(handle));
	}
	catch (...)
	{
		if (image)
			AImage_delete(image);
	}
}

std::shared_ptr<decoder::mapped_hardware_buffer> decoder::map_hardware_buffer(AImage * image)
{
	AHardwareBuffer * hardware_buffer;
	check(AImage_getHardwareBuffer(image, &hardware_buffer), "AImage_getHardwareBuffer");

	std::unique_lock lock(hbm_mutex);

	AHardwareBuffer_Desc buffer_desc{};
	AHardwareBuffer_describe(hardware_buffer, &buffer_desc);

	auto [properties, format_properties] = device.getAndroidHardwareBufferPropertiesANDROID<vk::AndroidHardwareBufferPropertiesANDROID, vk::AndroidHardwareBufferFormatPropertiesANDROID>(*hardware_buffer);

	if (!pipeline || memcmp(&pipeline->ahb_format, &format_properties, sizeof(format_properties)))
	{
		pipeline.reset();
		pipeline = std::make_shared<pipeline_context>(device, buffer_desc, format_properties, renderpass, description);
		hardware_buffer_map.clear();
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
			.initialLayout = vk::ImageLayout::eUndefined
		},
		vk::ExternalMemoryImageCreateInfo{
			.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID
		},
		vk::ExternalFormatANDROID{
			.externalFormat = format_properties.externalFormat
		}
	};

	vk::raii::Image vimage(device, img_info.get());

	assert(properties.memoryTypeBits != 0);
	vk::StructureChain mem_info{
		vk::MemoryAllocateInfo{
			.allocationSize = properties.allocationSize,
			.memoryTypeIndex = (uint32_t)(ffs(properties.memoryTypeBits) - 1)
		},
		vk::MemoryDedicatedAllocateInfo{
			.image = *vimage
		},
		vk::ImportAndroidHardwareBufferInfoANDROID{
			.buffer = hardware_buffer
		}
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
				.layerCount = 1
			}
		},
		vk::SamplerYcbcrConversionInfo{
			.conversion = *pipeline->ycbcr_conversion
		}
	};


	application::ignore_debug_reports_for(*vimage);
	vk::raii::ImageView image_view(device, iv_info.get());
	application::unignore_debug_reports_for(*vimage);

	vk::raii::DescriptorSet descriptor_set = [&]{
		std::unique_lock lock(pipeline->descriptor_pool_mutex);

		vk::raii::DescriptorSets descriptor_sets(device, {
			.descriptorPool = *pipeline->descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &*pipeline->descriptor_set_layout,
		});

		return std::move(descriptor_sets[0]);
	}();

	vk::DescriptorImageInfo image_info{
	        .imageView = *image_view,
	        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};

	vk::WriteDescriptorSet descriptor_write{
	        .dstSet = *descriptor_set,
	        .dstBinding = 0,
	        .dstArrayElement = 0,
	        .descriptorCount = 1,
	        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	        .pImageInfo = &image_info,
	};

	device.updateDescriptorSets(descriptor_write, {});

	auto handle = std::make_shared<mapped_hardware_buffer>();
	handle->pipeline = pipeline;
	handle->vimage = std::move(vimage);
	handle->image_view = std::move(image_view);
	handle->memory = std::move(memory);
	handle->descriptor_set = std::move(descriptor_set);

	hardware_buffer_map[hardware_buffer] = handle;
	return handle;
}

void decoder::on_media_error(AMediaCodec *, void * userdata, media_status_t error, int32_t actionCode, const char * detail)
{
	spdlog::warn("Mediacodec error: {}", detail);
}
void decoder::on_media_format_changed(AMediaCodec *, void * userdata, AMediaFormat *)
{
	spdlog::info("Mediacodec format changed");
}
void decoder::on_media_input_available(AMediaCodec *, void * userdata, int32_t index)
{
	auto self = (decoder *)userdata;
	self->input_buffers.push(index);
}
void decoder::on_media_output_available(AMediaCodec * media_codec, void * userdata, int32_t index, AMediaCodecBufferInfo * bufferInfo)
{
	auto self = (decoder *)userdata;
	self->output_buffers.push(index);
	// will be consumed by dedicated thread
}

void decoder::blit(vk::raii::CommandBuffer& command_buffer, blit_handle & handle, std::span<int> target_indices)
{
	if (handle.vk_data->layout != vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		vk::ImageMemoryBarrier memory_barrier{
			.srcAccessMask = {},
			.dstAccessMask = vk::AccessFlagBits::eShaderRead,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.image = *handle.vk_data->vimage,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};

		command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, memory_barrier);

		handle.vk_data->layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	}

	for (size_t target_index: target_indices)
	{
		assert(target_index < blit_targets.size());

		auto & target = blit_targets[target_index];
		if (description.offset_x > target.offset.x + target.extent.width)
			continue;
		if (description.offset_x + description.width < target.offset.x)
			continue;

		int x0 = description.offset_x - target.offset.x;
		int y0 = description.offset_y;
		int x1 = x0 + description.width;
		int y1 = y0 + description.height;

		vk::RenderPassBeginInfo begin_info{
		        .renderPass = *renderpass,
		        .framebuffer = **target.framebuffer,
		        .renderArea = {
		                        .offset = {0, 0},
		                        .extent = target.extent,
		                },
		        .clearValueCount = 0,
		};

		command_buffer.beginRenderPass(begin_info, vk::SubpassContents::eInline);
		command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *handle.vk_data->pipeline->pipeline);

		vk::Viewport viewport{
			.x = (float)x0,
			.y = (float)y0,
			.width = (float)description.width,
			.height = (float)description.height,
			.minDepth = 0,
			.maxDepth = 1
		};

		x0 = std::clamp<int>(x0, 0, target.extent.width);
		x1 = std::clamp<int>(x1, 0, target.extent.width);
		y0 = std::clamp<int>(y0, 0, target.extent.height);
		y1 = std::clamp<int>(y1, 0, target.extent.height);

		vk::Rect2D scissor{
		        .offset = {.x = x0, .y = y0},
		        .extent = {.width = (uint32_t)(x1 - x0), .height = (uint32_t)(y1 - y0)},
		};

		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, scissor);

		command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *handle.vk_data->pipeline->layout, 0, *handle.vk_data->descriptor_set, {});
		command_buffer.draw(3, 1, 0, 0);

		command_buffer.endRenderPass();
	}
}

decoder::blit_handle::~blit_handle()
{
	AImage_delete(aimage);
}

} // namespace wivrn::android
