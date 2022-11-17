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
#include "utils/check.h"
#include "vk/device_memory.h"
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
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_core.h>

#include "external/magic_enum.hpp"

DEUGLIFY(AMediaFormat)

struct wivrn::android::decoder::pipeline_context
{
	VkDevice device;
	VkAndroidHardwareBufferFormatPropertiesANDROID ahb_format;

	VkSamplerYcbcrConversion ycbcr_conversion;
	VkSampler sampler;

	std::vector<VkFramebuffer> framebuffers;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorPool descriptor_pool;
	std::mutex descriptor_pool_mutex;
	vk::pipeline_layout layout;
	vk::pipeline pipeline;

	~pipeline_context()
	{
		vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
		vkDestroySampler(device, sampler, nullptr);
		vkDestroySamplerYcbcrConversion(device, ycbcr_conversion, nullptr);
	}

	pipeline_context(VkDevice device, VkAndroidHardwareBufferFormatPropertiesANDROID & ahb_format, VkRenderPass renderpass, const to_headset::video_stream_description::item& description) :
	        device(device), ahb_format(ahb_format)
	{
		assert(ahb_format.externalFormat != 0);
		spdlog::info("AndroidHardwareBufferProperties");
		spdlog::info("  Vulkan format: {}", magic_enum::enum_name(ahb_format.format));
		spdlog::info("  External format: {:#x}", ahb_format.externalFormat);
		spdlog::info("  Format features: {:#x}", ahb_format.formatFeatures);
		spdlog::info("  samplerYcbcrConversionComponents: ({}, {}, {}, {})",
		             magic_enum::enum_name(ahb_format.samplerYcbcrConversionComponents.r),
		             magic_enum::enum_name(ahb_format.samplerYcbcrConversionComponents.g),
		             magic_enum::enum_name(ahb_format.samplerYcbcrConversionComponents.b),
		             magic_enum::enum_name(ahb_format.samplerYcbcrConversionComponents.a));
		spdlog::info("  Suggested YCbCr model: {}", magic_enum::enum_name(ahb_format.suggestedYcbcrModel));
		spdlog::info("  Suggested YCbCr range: {}", magic_enum::enum_name(ahb_format.suggestedYcbcrRange));
		spdlog::info("  Suggested X chroma offset: {}", magic_enum::enum_name(ahb_format.suggestedXChromaOffset));
		spdlog::info("  Suggested Y chroma offset: {}", magic_enum::enum_name(ahb_format.suggestedYChromaOffset));

		VkFilter yuv_filter;
		if (ahb_format.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
			yuv_filter = VK_FILTER_LINEAR;
		else
			yuv_filter = VK_FILTER_NEAREST;

		// Create VkSamplerYcbcrConversion
		VkExternalFormatANDROID ycbcr_create_info2{.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
		                                           .externalFormat = ahb_format.externalFormat};

		VkSamplerYcbcrConversionCreateInfo ycbcr_create_info{
		        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
		        .pNext = &ycbcr_create_info2,
		        .format = VK_FORMAT_UNDEFINED,
		        .ycbcrModel = ahb_format.suggestedYcbcrModel,
		        .ycbcrRange = ahb_format.suggestedYcbcrRange,
		        .components = ahb_format.samplerYcbcrConversionComponents,
		        .xChromaOffset = ahb_format.suggestedXChromaOffset,
		        .yChromaOffset = ahb_format.suggestedYChromaOffset,
		        .chromaFilter = yuv_filter,
		};
		// suggested values from decoder don't actually read the metadata, so it's garbage
		if (description.range)
			ycbcr_create_info.ycbcrRange = VkSamplerYcbcrRange(*description.range);
		if (description.color_model)
			ycbcr_create_info.ycbcrModel = VkSamplerYcbcrModelConversion(*description.color_model);

		CHECK_VK(vkCreateSamplerYcbcrConversion(device, &ycbcr_create_info, nullptr, &ycbcr_conversion));

		// Create VkSampler
		VkSamplerYcbcrConversionInfo sampler_info2{
		        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
		        .conversion = ycbcr_conversion,
		};

		VkSamplerCreateInfo sampler_info{
		        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		        .pNext = &sampler_info2,
		        .magFilter = yuv_filter,
		        .minFilter = yuv_filter,
		        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		        .mipLodBias = 0.0f,
		        .anisotropyEnable = VK_FALSE,
		        .maxAnisotropy = 1,
		        .compareEnable = VK_FALSE,
		        .compareOp = VK_COMPARE_OP_NEVER,
		        .minLod = 0.0f,
		        .maxLod = 0.0f,
		        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, // TODO TBC
		        .unnormalizedCoordinates = VK_FALSE,

		};

		CHECK_VK(vkCreateSampler(device, &sampler_info, nullptr, &sampler));

		// Create VkDescriptorSetLayout with an immutable sampler
		VkDescriptorSetLayoutBinding samplerLayoutBinding{
		        .binding = 0,
		        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		        .descriptorCount = 1,
		        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		        .pImmutableSamplers = &sampler,
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo{
		        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		        .bindingCount = 1,
		        .pBindings = &samplerLayoutBinding,
		};

		CHECK_VK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptor_set_layout));

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 100;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = poolSize.descriptorCount;
		CHECK_VK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptor_pool));

		// Create graphics pipeline
		vk::shader vertex_shader(device, "stream.vert");
		vk::shader fragment_shader(device, "stream.frag");

		VkPipelineColorBlendAttachmentState pcbas{};
		pcbas.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;

		layout = vk::pipeline_layout(
		        device, {.descriptor_set_layouts = {descriptor_set_layout}});

		vk::pipeline::graphics_info pipeline_info{
		        .shader_stages =
		                {{.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main"},
		                 {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main"}},
		        .vertex_input_bindings = {},
		        .vertex_input_attributes = {},
		        .InputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
		        .viewports = {{}},
		        .scissors = {{}},
		        .RasterizationState = {.polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1},
		        .MultisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
		        .ColorBlendState = {.attachmentCount = 1, .pAttachments = &pcbas},
		        .dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
		        .renderPass = renderpass,
		        .subpass = 0,
		};

		pipeline = vk::pipeline(device, pipeline_info, layout);
	}
};

struct wivrn::android::decoder::mapped_hardware_buffer
{
	std::shared_ptr<pipeline_context> pipeline;
	VkImageView image_view{};
	VkImage vimage{};
	VkDeviceMemory memory{};
	VkDescriptorSet descriptor_set{};
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

	~mapped_hardware_buffer()
	{
		assert(pipeline);
		auto device = pipeline->device;
		{
			std::unique_lock lock(pipeline->descriptor_pool_mutex);
			vkFreeDescriptorSets(device, pipeline->descriptor_pool, 1, &descriptor_set);
		}
		vkDestroyImageView(device, image_view, nullptr);
		vkFreeMemory(device, memory, nullptr);
		vkDestroyImage(device, vimage, nullptr);
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

void decoder::push_nals(std::span<uint8_t> data, int64_t timestamp, uint32_t flags)
{
	while (!data.empty())
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

		size = std::min(data.size(), size);

		memcpy(buffer, data.data(), size);

		data = data.subspan(size);

		t1 = application::now();
		check(AMediaCodec_queueInputBuffer(media_codec.get(), input_buffer, 0 /* offset */, size, timestamp, flags),
		      "AMediaCodec_queueInputBuffer");
		t2 = application::now();
		if (t2 - t1 > 1'000'000)
			spdlog::warn("AMediaCodec_queueInputBuffer() took {}µs", (t2 - t1) / 1000);
	}
}

decoder::decoder(
        VkDevice device,
        VkPhysicalDevice physical_device,
        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
        float fps,
        std::weak_ptr<scenes::stream> weak_scene,
        shard_accumulator * accumulator) :
        description(description), fps(fps), device(device), weak_scene(weak_scene), accumulator(accumulator)
{
	AImageReader * ir;
	check(AImageReader_newWithUsage(description.width, description.height, AIMAGE_FORMAT_PRIVATE, AHARDWAREBUFFER_USAGE_CPU_READ_NEVER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 5 /* maxImages */, &ir),
	      "AImageReader_newWithUsage");
	image_reader.reset(ir);

	AImageReader_ImageListener listener{this, on_image_available};
	check(AImageReader_setImageListener(ir, &listener), "AImageReader_setImageListener");

	vkGetAndroidHardwareBufferPropertiesANDROID =
	        application::get_vulkan_proc<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
	                "vkGetAndroidHardwareBufferPropertiesANDROID");

	output_releaser = std::thread([this]() {
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
	output_releaser.join();
	for (auto & i: blit_targets)
		vkDestroyFramebuffer(device, i.framebuffer, nullptr);
}

void decoder::set_blit_targets(std::vector<decoder::blit_target> targets, VkFormat format)
{
	// Create renderpass
	VkAttachmentReference color_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	vk::renderpass::info renderpass_info{.attachments = {VkAttachmentDescription{
	                                             .format = format,
	                                             .samples = VK_SAMPLE_COUNT_1_BIT,
	                                             .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
	                                             .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	                                             .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                             .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                     }},
	                                     .subpasses = {VkSubpassDescription{
	                                             .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                             .colorAttachmentCount = 1,
	                                             .pColorAttachments = &color_ref,
	                                     }},
	                                     .dependencies = {}};

	renderpass = vk::renderpass(device, renderpass_info);

	// Remove old blit targets first
	for (auto & i: blit_targets)
		vkDestroyFramebuffer(device, i.framebuffer, nullptr);

	blit_targets = std::move(targets);

	for (auto & i: blit_targets)
	{
		VkFramebufferCreateInfo fb_create_info{};
		fb_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_create_info.renderPass = renderpass;
		fb_create_info.attachmentCount = 1;
		fb_create_info.pAttachments = &i.image_view;
		fb_create_info.width = i.extent.width;
		fb_create_info.height = i.extent.height;
		fb_create_info.layers = 1;
		CHECK_VK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &i.framebuffer));
	}
}

void decoder::push_data(std::span<uint8_t> data, uint64_t frame_index, bool partial)
{
	auto [csd, not_csd] = filter_csd(data, description.codec);

	if (!media_codec)
	{
		if (csd.empty())
		{
			// TODO request I frame
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

	if (!csd.empty())
		push_nals(csd, 0, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);

	uint64_t fake_timestamp_us = frame_index * 10'000;
	push_nals(not_csd, fake_timestamp_us, partial ? AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME : 0);
}

void decoder::frame_completed(xrt::drivers::wivrn::from_headset::feedback & feedback, const xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
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

	std::lock_guard lock(hbm_mutex);

	AHardwareBuffer_Desc buffer_desc{};
	AHardwareBuffer_describe(hardware_buffer, &buffer_desc);

	VkAndroidHardwareBufferFormatPropertiesANDROID format_properties{};
	format_properties.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

	VkAndroidHardwareBufferPropertiesANDROID properties{};
	properties.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
	properties.pNext = &format_properties;
	CHECK_VK(vkGetAndroidHardwareBufferPropertiesANDROID(device, hardware_buffer, &properties));

	if (!pipeline || memcmp(&pipeline->ahb_format, &format_properties, sizeof(format_properties)))
	{
		pipeline.reset();
		pipeline = std::make_shared<pipeline_context>(device, format_properties, renderpass, description);
		hardware_buffer_map.clear();
	}

	auto it = hardware_buffer_map.find(hardware_buffer);
	if (it != hardware_buffer_map.end())
		return it->second;

	VkExternalFormatANDROID img_info3{.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
	                                  .externalFormat = format_properties.externalFormat};

	VkExternalMemoryImageCreateInfo img_info2{
	        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
	        .pNext = &img_info3,
	        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};

	VkImageCreateInfo img_info{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	                           .pNext = &img_info2,
	                           .flags = 0,
	                           .imageType = VK_IMAGE_TYPE_2D,
	                           .format = VK_FORMAT_UNDEFINED,
	                           .extent = {buffer_desc.width, buffer_desc.height, 1},
	                           .mipLevels = 1,
	                           .arrayLayers = 1,
	                           .samples = VK_SAMPLE_COUNT_1_BIT,
	                           .tiling = VK_IMAGE_TILING_OPTIMAL,
	                           .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
	                           .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	                           .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

	vk::image vimage(device, img_info);

	VkImportAndroidHardwareBufferInfoANDROID mem_info3{
	        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
	        .buffer = hardware_buffer};

	VkMemoryDedicatedAllocateInfo mem_info2{.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
	                                        .pNext = &mem_info3,
	                                        .image = vimage};

	assert(properties.memoryTypeBits != 0);
	VkMemoryAllocateInfo mem_info{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	                              .pNext = &mem_info2,
	                              .allocationSize = properties.allocationSize,
	                              .memoryTypeIndex = (uint32_t)(ffs(properties.memoryTypeBits) - 1)};

	vk::device_memory memory(device, mem_info);
	CHECK_VK(vkBindImageMemory(device, vimage, memory, 0));

	VkSamplerYcbcrConversionInfo ycbcr_info{.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
	                                        .conversion = pipeline->ycbcr_conversion};

	VkImageViewCreateInfo iv_info{
	        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	        .pNext = &ycbcr_info,
	        .image = vimage,
	        .viewType = VK_IMAGE_VIEW_TYPE_2D,
	        .format = VK_FORMAT_UNDEFINED,
	        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
	        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	                             .baseMipLevel = 0,
	                             .levelCount = 1,
	                             .baseArrayLayer = 0,
	                             .layerCount = 1}};

	// TODO: vk::image_view
	VkImageView image_view;
	application::ignore_debug_reports_for(vimage);
	CHECK_VK(vkCreateImageView(device, &iv_info, nullptr, &image_view));
	application::unignore_debug_reports_for(vimage);

	// TODO: vk::descriptor_set
	VkDescriptorSet descriptor_set;
	VkDescriptorSetAllocateInfo descriptor_info{
	        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	        .descriptorPool = pipeline->descriptor_pool,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &pipeline->descriptor_set_layout,
	};

	{
		std::unique_lock lock(pipeline->descriptor_pool_mutex);
		CHECK_VK(vkAllocateDescriptorSets(device, &descriptor_info, &descriptor_set));
	}

	VkDescriptorImageInfo image_info{
	        .imageView = image_view,
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet descriptor_write{
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = 0,
	        .dstArrayElement = 0,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);

	auto handle = std::make_shared<mapped_hardware_buffer>();
	handle->pipeline = pipeline;
	handle->vimage = vimage.release();
	handle->image_view = image_view;
	handle->memory = memory.release();
	handle->descriptor_set = descriptor_set;

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

void decoder::blit(VkCommandBuffer command_buffer, blit_handle & handle, std::span<int> target_indices)
{
	if (handle.vk_data->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		VkImageMemoryBarrier memory_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		                                    .srcAccessMask = 0,
		                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		                                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		                                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                                    .image = handle.vk_data->vimage,
		                                    .subresourceRange = {
		                                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                                            .baseMipLevel = 0,
		                                            .levelCount = 1,
		                                            .baseArrayLayer = 0,
		                                            .layerCount = 1,
		                                    }};

		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &memory_barrier);

		handle.vk_data->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

		VkRenderPassBeginInfo begin_info{
		        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		        .renderPass = renderpass,
		        .framebuffer = target.framebuffer,
		        .renderArea =
		                {
		                        .offset = {0, 0},
		                        .extent = target.extent,
		                },
		        .clearValueCount = 0,
		};

		vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, handle.vk_data->pipeline->pipeline);

		VkViewport viewport{.x = (float)x0,
		                    .y = (float)y0,
		                    .width = (float)description.width,
		                    .height = (float)description.height,
		                    .minDepth = 0,
		                    .maxDepth = 1};

		x0 = std::clamp<int>(x0, 0, target.extent.width);
		x1 = std::clamp<int>(x1, 0, target.extent.width);
		y0 = std::clamp<int>(y0, 0, target.extent.height);
		y1 = std::clamp<int>(y1, 0, target.extent.height);

		VkRect2D scissor{
		        .offset = {.x = x0, .y = y0},
		        .extent = {.width = (uint32_t)(x1 - x0), .height = (uint32_t)(y1 - y0)},
		};

		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, handle.vk_data->pipeline->layout, 0, 1, &handle.vk_data->descriptor_set, 0, nullptr);
		vkCmdDraw(command_buffer, 3, 1, 0, 0);

		vkCmdEndRenderPass(command_buffer);
	}
}

decoder::blit_handle::~blit_handle()
{
	AImage_delete(aimage);
}

} // namespace wivrn::android
