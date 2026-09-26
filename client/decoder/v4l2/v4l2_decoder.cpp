/*
 * WiVRn VR streaming
 * Copyright (C) 2026 galister <galister-dev@pm.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "v4l2_decoder.h"

#include "application.h"
#include "scenes/stream.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace wivrn::v4l2
{
namespace
{
constexpr auto output_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
constexpr auto capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
constexpr unsigned int output_buffer_count = 8;
constexpr unsigned int capture_buffer_count = 12;
constexpr uint64_t drm_format_mod_linear = 0;

int xioctl(int fd, unsigned long request, void * arg)
{
	int result;
	do
		result = ioctl(fd, request, arg);
	while (result < 0 && errno == EINTR);
	return result;
}

void ioctl_or_throw(int fd, unsigned long request, void * arg, const char * what)
{
	if (xioctl(fd, request, arg) < 0)
		throw std::system_error(errno, std::generic_category(), what);
}

v4l2_buffer make_v4l2_buffer(v4l2_buf_type type, uint32_t index, std::array<v4l2_plane, VIDEO_MAX_PLANES> & planes)
{
	return {
	        .index = index,
	        .type = type,
	        .bytesused = 0,
	        .memory = V4L2_MEMORY_MMAP,
	        .m = {.planes = planes.data()},
	        .length = static_cast<__u32>(planes.size()),
	};
}

uint32_t v4l2_codec(wivrn::video_codec codec)
{
	switch (codec)
	{
		case wivrn::video_codec::h264:
			return V4L2_PIX_FMT_H264;
		case wivrn::video_codec::h265:
			return V4L2_PIX_FMT_HEVC;
		default:
			throw std::runtime_error("No V4L2 codec mapping for requested codec");
	}
}

bool is_iris_decoder(int fd)
{
	v4l2_capability caps{};
	if (xioctl(fd, VIDIOC_QUERYCAP, &caps) < 0)
		return false;
	const uint32_t capabilities = caps.capabilities & V4L2_CAP_DEVICE_CAPS ? caps.device_caps : caps.capabilities;
	return std::strcmp(reinterpret_cast<const char *>(caps.driver), "iris_driver") == 0 &&
	       (capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) && (capabilities & V4L2_CAP_STREAMING);
}

bool supports_format(int fd, uint32_t pixel_format)
{
	for (uint32_t index = 0;; ++index)
	{
		v4l2_fmtdesc fmt{};
		fmt.index = index;
		fmt.type = output_type;
		if (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) < 0)
			return false;
		if (fmt.pixelformat == pixel_format)
			return true;
	}
}

wivrn::fd_base find_device(wivrn::video_codec codec)
{
	const uint32_t pixel_format = v4l2_codec(codec);

	for (unsigned int i = 0; i < 128; ++i)
	{
		const std::string path = std::format("/dev/video/{}", i);

		int probe_fd =
		        open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);

		if (probe_fd < 0)
			continue;

		const bool suitable =
		        is_iris_decoder(probe_fd) &&
		        supports_format(probe_fd, pixel_format);

		close(probe_fd);

		if (!suitable)
			continue;

		// actual decoder gets a fresh context. breaks without
		int decoder_fd =
		        open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);

		if (decoder_fd < 0)
			throw std::runtime_error(
			        "Failed to reopen " + path + ": " +
			        std::strerror(errno));

		return wivrn::fd_base{decoder_fd};
	}

	throw std::runtime_error(
	        "No iris V4L2 decoder supports the requested codec");
}

bool has_extension(std::string_view name)
{
	const auto & extensions = application::get_vk_device_extensions();
	return std::ranges::any_of(extensions, [name](const char * extension) { return name == extension; });
}

void require_dmabuf_extensions()
{
	for (const char * extension: {
	             VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
	             VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
	             VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
	             VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
	     })
	{
		if (!has_extension(extension))
			throw std::runtime_error(std::string("V4L2 DMA-buf decoder requires Vulkan extension ") + extension);
	}
}

uint32_t memory_type_index(vk::raii::PhysicalDevice & physical_device, uint32_t bits)
{
	const auto memory_properties = physical_device.getMemoryProperties();
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
		if (bits & (1u << i))
			return i;
	throw std::runtime_error("No Vulkan memory type can import iris DMA-buf");
}

vk::Format vk_format_for_capture(uint32_t pixel_format)
{
	switch (pixel_format)
	{
		case V4L2_PIX_FMT_NV12:
			return vk::Format::eG8B8R82Plane420Unorm;
		case V4L2_PIX_FMT_P010:
			return vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16;
		default:
			return vk::Format::eUndefined;
	}
}

const char * capture_format_name(uint32_t pixel_format)
{
	switch (pixel_format)
	{
		case V4L2_PIX_FMT_NV12:
			return "NV12 (8-bit)";
		case V4L2_PIX_FMT_P010:
			return "P010 (10-bit)";
		default:
			return "unsupported";
	}
}

uint64_t timestamp_to_u64(const timeval & ts)
{
	return uint64_t(ts.tv_sec) * 1'000'000ull + uint64_t(ts.tv_usec);
}

void u64_to_timestamp(uint64_t value, timeval & ts)
{
	ts.tv_sec = value / 1'000'000ull;
	ts.tv_usec = value % 1'000'000ull;
}
} // namespace

struct decoder::v4l2_blit_handle : public wivrn::decoder::blit_handle
{
	unsigned int capture_index;
	decoder * self;

	v4l2_blit_handle(const wivrn::from_headset::feedback & feedback,
	                 const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	                 vk::ImageView image_view,
	                 vk::Image image,
	                 vk::Extent2D extent,
	                 vk::ImageLayout & current_layout,
	                 unsigned int capture_index,
	                 decoder * self) :
	        wivrn::decoder::blit_handle{feedback, view_info, image_view, image, extent, current_layout},
	        capture_index(capture_index),
	        self(self)
	{
		foreign_queue_family = VK_QUEUE_FAMILY_FOREIGN_EXT;
	}

	~v4l2_blit_handle()
	{
		self->release_capture_buffer(capture_index);
	}
};

decoder::decoder(vk::raii::Device & device,
                 vk::raii::PhysicalDevice & physical_device,
                 uint32_t vk_queue_family_index,
                 const wivrn::to_headset::video_stream_description & description,
                 uint8_t stream_index,
                 std::weak_ptr<scenes::stream> scene,
                 shard_accumulator * accumulator) :
        device(device),
        physical_device(physical_device),
        weak_scene(scene),
        accumulator(accumulator),
        extent{description.width, description.height / (stream_index == 2 ? 2u : 1u)},
        codec_type(description.codec[stream_index]),
        vk_queue_family_index(vk_queue_family_index)
{
	require_dmabuf_extensions();

	setup_device(find_device(codec_type));
	int raw_wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (raw_wake_fd < 0)
		throw std::runtime_error("eventfd failed for iris decoder: " + std::string(std::strerror(errno)));
	wake_fd = wivrn::fd_base(raw_wake_fd);
	worker = std::jthread([this](std::stop_token stop_token) { worker_loop(stop_token); });
}

decoder::~decoder()
{
	abandon_assembling_frame();

	if (worker.joinable())
	{
		worker.request_stop();
		wake_worker();
		worker.join();
	}

	if (!fd)
		return;
	if (capture_streaming)
	{
		v4l2_buf_type type = capture_type;
		xioctl(fd.get_fd(), VIDIOC_STREAMOFF, &type);
	}
	if (output_streaming)
	{
		v4l2_buf_type type = output_type;
		xioctl(fd.get_fd(), VIDIOC_STREAMOFF, &type);
	}
	capture_buffers.clear();
}

void decoder::setup_device(wivrn::fd_base device_fd)
{
	fd = std::move(device_fd);

	v4l2_event_subscription sub{.type = V4L2_EVENT_SOURCE_CHANGE};
	ioctl_or_throw(fd.get_fd(), VIDIOC_SUBSCRIBE_EVENT, &sub, "VIDIOC_SUBSCRIBE_EVENT(SOURCE_CHANGE)");
	setup_output_queue();

	// H265 needs an initial header-parsing phase with only the coded OUTPUT queue active.
	// setting up and streaming he placeholder queue can prevent iris from completing
	// sequence initialization
	if (codec_type != wivrn::video_codec::h265)
	{
		setup_capture_queue();
		v4l2_buf_type capture = capture_type;
		ioctl_or_throw(fd.get_fd(), VIDIOC_STREAMON, &capture, "VIDIOC_STREAMON(CAPTURE)");
		capture_streaming = true;
	}

	v4l2_buf_type output = output_type;
	ioctl_or_throw(fd.get_fd(), VIDIOC_STREAMON, &output, "VIDIOC_STREAMON(OUTPUT)");
	output_streaming = true;
}

void decoder::setup_output_queue()
{
	v4l2_format fmt{
	        .type = output_type,
	        .fmt = {
	                .pix_mp = {
	                        .width = extent.width,
	                        .height = extent.height,
	                        .pixelformat = v4l2_codec(codec_type),
	                        .field = V4L2_FIELD_NONE,
	                        .plane_fmt = {{
	                                .sizeimage = std::max<uint32_t>(extent.width * extent.height, 4 * 1024 * 1024),
	                        }},
	                        .num_planes = 1,
	                }},
	};
	ioctl_or_throw(fd.get_fd(), VIDIOC_S_FMT, &fmt, "VIDIOC_S_FMT(OUTPUT)");
	if (fmt.fmt.pix_mp.pixelformat != v4l2_codec(codec_type))
		throw std::runtime_error("iris decoder rejected requested coded format");

	v4l2_requestbuffers req{.count = output_buffer_count, .type = output_type, .memory = V4L2_MEMORY_MMAP};
	ioctl_or_throw(fd.get_fd(), VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS(OUTPUT)");
	if (!req.count)
		throw std::runtime_error("iris decoder allocated no OUTPUT buffers");

	output_buffers.resize(req.count);
	free_output_buffers.reserve(req.count);
	for (uint32_t i = 0; i < req.count; ++i)
	{
		std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
		v4l2_buffer buf = make_v4l2_buffer(output_type, i, planes);
		ioctl_or_throw(fd.get_fd(), VIDIOC_QUERYBUF, &buf, "VIDIOC_QUERYBUF(OUTPUT)");
		output_buffers[i].planes.resize(buf.length);
		for (uint32_t p = 0; p < buf.length; ++p)
		{
			void * address = mmap(nullptr, planes[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get_fd(), planes[p].m.mem_offset);
			if (address == MAP_FAILED)
				throw std::runtime_error("mmap OUTPUT failed: " + std::string(std::strerror(errno)));
			output_buffers[i].planes[p] = {address, planes[p].length};
		}
		free_output_buffers.push_back(i);
	}
} // namespace wivrn::v4l2

void decoder::import_capture_buffer(unsigned int index, const std::vector<size_t> & plane_lengths)
{
	if (capture_vk_format == vk::Format::eUndefined || capture_num_planes != 1 || plane_lengths.size() != 1)
		throw std::runtime_error("iris DMA-buf path requires single-memory-plane NV12 or P010 CAPTURE output");

	v4l2_exportbuffer exp{
	        .type = capture_type,
	        .index = index,
	        .plane = 0,
	        .flags = O_CLOEXEC,
	};
	ioctl_or_throw(fd.get_fd(), VIDIOC_EXPBUF, &exp, "VIDIOC_EXPBUF(CAPTURE)");
	int dma_fd = exp.fd;

	const vk::DeviceSize chroma_offset = vk::DeviceSize(capture_bytesperline) * capture_height;
	if (chroma_offset >= plane_lengths[0])
	{
		close(dma_fd);
		throw std::runtime_error("iris CAPTURE buffer is too small for its reported stride/height");
	}

	std::array<vk::SubresourceLayout, 2> plane_layouts{{
	        {
	                .offset = 0,
	                .size = chroma_offset,
	                .rowPitch = capture_bytesperline,
	        },
	        {
	                .offset = chroma_offset,
	                .size = plane_lengths[0] - chroma_offset,
	                .rowPitch = capture_bytesperline,
	        },
	}};

	vk::StructureChain image_info{
	        vk::ImageCreateInfo{
	                .imageType = vk::ImageType::e2D,
	                .format = capture_vk_format,
	                .extent = {capture_width, capture_height, 1},
	                .mipLevels = 1,
	                .arrayLayers = 1,
	                .samples = vk::SampleCountFlagBits::e1,
	                .tiling = vk::ImageTiling::eDrmFormatModifierEXT,
	                .usage = vk::ImageUsageFlagBits::eSampled,
	                .sharingMode = vk::SharingMode::eExclusive,
	                .initialLayout = vk::ImageLayout::eUndefined,
	        },
	        vk::ExternalMemoryImageCreateInfo{
	                .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
	        },
	        vk::ImageDrmFormatModifierExplicitCreateInfoEXT{
	                .drmFormatModifier = drm_format_mod_linear,
	                .drmFormatModifierPlaneCount = uint32_t(plane_layouts.size()),
	                .pPlaneLayouts = plane_layouts.data(),
	        },
	};

	auto image = vk::raii::Image(device, image_info.get());
	auto [requirements, dedicated] = device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>({.image = *image});
	auto fd_properties = device.getMemoryFdPropertiesKHR(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, dma_fd);
	const uint32_t memory_bits = requirements.memoryRequirements.memoryTypeBits & fd_properties.memoryTypeBits;
	if (!memory_bits)
	{
		close(dma_fd);
		throw std::runtime_error("No compatible Vulkan memory type for iris DMA-buf");
	}

	vk::StructureChain allocation_info{
	        vk::MemoryAllocateInfo{
	                .allocationSize = requirements.memoryRequirements.size,
	                .memoryTypeIndex = memory_type_index(physical_device, memory_bits),
	        },
	        vk::ImportMemoryFdInfoKHR{
	                .handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
	                .fd = dma_fd,
	        },
	        vk::MemoryDedicatedAllocateInfo{
	                .image = *image,
	        },
	};
	if (!(dedicated.prefersDedicatedAllocation || dedicated.requiresDedicatedAllocation))
		allocation_info.unlink<vk::MemoryDedicatedAllocateInfo>();

	vk::raii::DeviceMemory memory = nullptr;
	try
	{
		memory = vk::raii::DeviceMemory(device, allocation_info.get());
	}
	catch (...)
	{
		// failed DMA-buf import
		close(dma_fd);
		throw;
	}
	image.bindMemory(*memory, 0);

	vk::StructureChain view_info{
	        vk::ImageViewCreateInfo{
	                .image = *image,
	                .viewType = vk::ImageViewType::e2D,
	                .format = capture_vk_format,
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
	auto image_view = vk::raii::ImageView(device, view_info.get());

	auto & capture = capture_buffers[index];
	capture.plane_lengths = plane_lengths;
	capture.memory = std::move(memory);
	capture.image = std::move(image);
	capture.image_view = std::move(image_view);
	capture.current_layout = vk::ImageLayout::eUndefined;
	capture.held_by_vulkan = false;
}

void decoder::initialize_capture_ownership()
{
	if (capture_buffers.empty())
		return;

	vk::raii::CommandPool command_pool{
	        device,
	        vk::CommandPoolCreateInfo{
	                .flags = vk::CommandPoolCreateFlagBits::eTransient,
	                .queueFamilyIndex = vk_queue_family_index,
	        }};
	vk::raii::CommandBuffer command_buffer = std::move(device.allocateCommandBuffers({
	        .commandPool = *command_pool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);
	vk::raii::Fence fence = device.createFence({});

	command_buffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	std::vector<vk::ImageMemoryBarrier> barriers;
	barriers.reserve(capture_buffers.size());
	for (auto & capture: capture_buffers)
	{
		barriers.push_back({
		        .srcAccessMask = vk::AccessFlagBits::eNone,
		        .dstAccessMask = vk::AccessFlagBits::eNone,
		        .oldLayout = vk::ImageLayout::eUndefined,
		        .newLayout = vk::ImageLayout::eGeneral,
		        .srcQueueFamilyIndex = vk_queue_family_index,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		        .image = *capture.image,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		});
		capture.current_layout = vk::ImageLayout::eGeneral;
	}
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
	                               vk::PipelineStageFlagBits::eBottomOfPipe,
	                               {},
	                               {},
	                               {},
	                               barriers);
	command_buffer.end();

	{
		auto queue = application::get_queue().lock();
		queue->submit(vk::SubmitInfo{
		                      .commandBufferCount = 1,
		                      .pCommandBuffers = &*command_buffer,
		              },
		              *fence);
	}
	if (device.waitForFences(*fence, true, UINT64_MAX) != vk::Result::eSuccess)
		throw std::runtime_error("Failed to initialize iris DMA-buf Vulkan ownership");
}

void decoder::queue_capture_buffer(unsigned int index)
{
	if (index >= capture_buffers.size())
		throw std::runtime_error("Invalid iris CAPTURE buffer index");
	auto & capture = capture_buffers[index];

	std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
	v4l2_buffer buf = make_v4l2_buffer(capture_type, index, planes);
	buf.length = capture.plane_lengths.size();
	for (uint32_t p = 0; p < buf.length; ++p)
	{
		planes[p].bytesused = 0;
		planes[p].length = capture.plane_lengths[p];
	}
	ioctl_or_throw(fd.get_fd(), VIDIOC_QBUF, &buf, "VIDIOC_QBUF(CAPTURE)");
	capture.held_by_vulkan = false;
}

void decoder::setup_ycbcr_sampler()
{
	if (capture_vk_format == vk::Format::eUndefined)
		throw std::runtime_error("Cannot create YCbCr sampler for unsupported iris CAPTURE format");

	ycbcr_sampler = nullptr;
	ycbcr_conversion = nullptr;

	ycbcr_conversion = vk::raii::SamplerYcbcrConversion(device, {
	                                                                    .format = capture_vk_format,
	                                                                    .ycbcrModel = vk::SamplerYcbcrModelConversion::eYcbcr709,
	                                                                    .ycbcrRange = vk::SamplerYcbcrRange::eItuFull,
	                                                                    .chromaFilter = vk::Filter::eNearest,
	                                                            });

	vk::StructureChain sampler_info{
	        vk::SamplerCreateInfo{
	                .magFilter = vk::Filter::eNearest,
	                .minFilter = vk::Filter::eNearest,
	                .mipmapMode = vk::SamplerMipmapMode::eNearest,
	                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
	                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
	                .maxAnisotropy = 1,
	        },
	        vk::SamplerYcbcrConversionInfo{
	                .conversion = *ycbcr_conversion,
	        },
	};
	ycbcr_sampler = vk::raii::Sampler(device, sampler_info.get());
}

void decoder::setup_capture_queue()
{
	v4l2_format fmt{};
	fmt.type = capture_type;
	ioctl_or_throw(fd.get_fd(), VIDIOC_G_FMT, &fmt, "VIDIOC_G_FMT(CAPTURE)");

	capture_width = fmt.fmt.pix_mp.width;
	capture_height = fmt.fmt.pix_mp.height;
	capture_pixelformat = fmt.fmt.pix_mp.pixelformat;
	capture_num_planes = fmt.fmt.pix_mp.num_planes;
	capture_bytesperline = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
	capture_sizeimage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	capture_vk_format = vk_format_for_capture(capture_pixelformat);
	if (capture_vk_format == vk::Format::eUndefined || capture_num_planes != 1)
		throw std::runtime_error("iris DMA-buf decoder requires single-memory-plane NV12 or P010 CAPTURE output");
	if (!capture_bytesperline)
		throw std::runtime_error("iris CAPTURE format reported zero bytesperline");

	setup_ycbcr_sampler();

	v4l2_requestbuffers req{.count = capture_buffer_count, .type = capture_type, .memory = V4L2_MEMORY_MMAP};
	ioctl_or_throw(fd.get_fd(), VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS(CAPTURE)");
	if (!req.count)
		throw std::runtime_error("iris decoder allocated no CAPTURE buffers");

	capture_buffers.resize(req.count);
	for (uint32_t i = 0; i < req.count; ++i)
	{
		std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
		v4l2_buffer buf = make_v4l2_buffer(capture_type, i, planes);
		ioctl_or_throw(fd.get_fd(), VIDIOC_QUERYBUF, &buf, "VIDIOC_QUERYBUF(CAPTURE)");
		std::vector<size_t> lengths(buf.length);
		for (uint32_t p = 0; p < buf.length; ++p)
			lengths[p] = planes[p].length;
		import_capture_buffer(i, lengths);
	}

	initialize_capture_ownership();
	for (uint32_t i = 0; i < capture_buffers.size(); ++i)
		queue_capture_buffer(i);

	spdlog::info("V4L2 iris DMA-buf capture: {}x{}, {}, stride {}, size {}, {} buffers",
	             capture_width,
	             capture_height,
	             capture_format_name(capture_pixelformat),
	             capture_bytesperline,
	             capture_sizeimage,
	             capture_buffers.size());
}

void decoder::teardown_capture_queue()
{
	if (std::ranges::any_of(capture_buffers, &capture_buffer::held_by_vulkan))
		throw std::runtime_error("iris source change while a DMA-buf CAPTURE image is still in use by Vulkan");

	if (capture_streaming)
	{
		v4l2_buf_type type = capture_type;
		xioctl(fd.get_fd(), VIDIOC_STREAMOFF, &type);
		capture_streaming = false;
	}

	// first destroy Vulkan imports so image views don't reference the YCbCr conversion
	capture_buffers.clear();
	ycbcr_sampler = nullptr;
	ycbcr_conversion = nullptr;
	capture_vk_format = vk::Format::eUndefined;
	v4l2_requestbuffers req{.count = 0, .type = capture_type, .memory = V4L2_MEMORY_MMAP};
	ioctl_or_throw(fd.get_fd(), VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS(CAPTURE free)");
}

void decoder::release_capture_buffer(unsigned int index)
{
	{
		std::scoped_lock lock(recycle_mutex);
		recycle_capture_queue.push_back(index);
	}
	wake_worker();
}

void decoder::recycle_capture_buffers()
{
	{
		std::scoped_lock lock(recycle_mutex);
		recycle_capture_local.swap(recycle_capture_queue);
	}

	for (unsigned int index: recycle_capture_local)
	{
		if (index >= capture_buffers.size())
			continue;
		if (!capture_buffers[index].held_by_vulkan)
			continue;
		queue_capture_buffer(index);
	}
	recycle_capture_local.clear();
}

void decoder::handle_events()
{
	while (true)
	{
		v4l2_event event{};
		if (xioctl(fd.get_fd(), VIDIOC_DQEVENT, &event) < 0)
		{
			if (errno == EAGAIN || errno == ENOENT)
				return;
			throw std::runtime_error("VIDIOC_DQEVENT failed: " + std::string(std::strerror(errno)));
		}
		if (event.type == V4L2_EVENT_SOURCE_CHANGE && (event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION))
		{
			if (capture_buffers.empty())
			{
				// iris has now parsed enough VPS/SPS/PPS data for G_FMT(CAPTURE) to describe the real stream
				spdlog::info("V4L2 iris decoder source change; configuring DMA-buf CAPTURE queue");
			}
			else
			{
				spdlog::info("V4L2 iris decoder source change; rebuilding DMA-buf CAPTURE queue");
				recycle_capture_buffers();
				teardown_capture_queue();
			}

			setup_capture_queue();
			v4l2_buf_type type = capture_type;
			ioctl_or_throw(fd.get_fd(), VIDIOC_STREAMON, &type, "VIDIOC_STREAMON(CAPTURE after source change)");
			capture_streaming = true;
		}
	}
}

void decoder::reclaim_output_buffers()
{
	while (true)
	{
		std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
		v4l2_buffer buf = make_v4l2_buffer(output_type, 0, planes);
		if (xioctl(fd.get_fd(), VIDIOC_DQBUF, &buf) < 0)
		{
			if (errno == EAGAIN)
				return;
			throw std::runtime_error("VIDIOC_DQBUF(OUTPUT) failed: " + std::string(std::strerror(errno)));
		}
		if (queued_output_count == 0)
			throw std::runtime_error("iris decoder reclaimed more OUTPUT buffers than were queued");
		--queued_output_count;
		{
			std::scoped_lock lock(output_mutex);
			free_output_buffers.push_back(buf.index);
		}
	}
}

void decoder::queue_output_buffer(const ready_output & ready)
{
	auto & mapped = output_buffers[ready.buffer_index].planes[0];

	std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
	v4l2_buffer buf = make_v4l2_buffer(output_type, ready.buffer_index, planes);
	buf.length = 1;
	planes[0].bytesused = ready.bytes_used;
	planes[0].length = mapped.length;
	buf.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	u64_to_timestamp(ready.pending.timestamp, buf.timestamp);
	ioctl_or_throw(fd.get_fd(), VIDIOC_QBUF, &buf, "VIDIOC_QBUF(OUTPUT)");
	++queued_output_count;
	pending_frames.push_back(ready.pending);
}

void decoder::queue_ready_frames()
{
	reclaim_output_buffers();
	while (true)
	{
		ready_output ready;
		{
			std::scoped_lock lock(input_mutex);
			if (input_queue.empty())
				return;
			ready = std::move(input_queue.front());
			input_queue.pop_front();
		}
		queue_output_buffer(ready);
	}
}

void decoder::wake_worker()
{
	if (!wake_fd)
		return;
	uint64_t value = 1;
	ssize_t ret;
	do
		ret = write(wake_fd.get_fd(), &value, sizeof(value));
	while (ret < 0 && errno == EINTR);
	if (ret < 0 && errno != EAGAIN)
		spdlog::warn("Failed to wake iris decoder worker: {}", std::strerror(errno));
}

void decoder::rethrow_worker_exception()
{
	std::exception_ptr error;
	{
		std::scoped_lock lock(worker_exception_mutex);
		error = worker_exception;
	}
	if (error)
		std::rethrow_exception(error);
}

void decoder::worker_loop(std::stop_token stop_token)
{
	try
	{
		while (!stop_token.stop_requested())
		{
			// CAPTURE buffers released by the render thread have already
			// passed the Vulkan fence and FOREIGN ownership release
			recycle_capture_buffers();
			handle_events();
			if (capture_streaming)
				drain_capture();
			queue_ready_frames();

			if (stop_token.stop_requested())
				break;

			std::array<pollfd, 2> pfds{{
			        {.fd = fd.get_fd(), .events = static_cast<short>(POLLIN | POLLPRI | (queued_output_count ? POLLOUT : 0))},
			        {.fd = wake_fd.get_fd(), .events = POLLIN},
			}};

			int result;
			do
				result = poll(pfds.data(), pfds.size(), -1);
			while (result < 0 && errno == EINTR);
			if (result < 0)
				throw std::runtime_error("poll failed for iris decoder: " + std::string(std::strerror(errno)));

			if (pfds[1].revents & POLLIN)
			{
				uint64_t value;
				while (read(wake_fd.get_fd(), &value, sizeof(value)) < 0 && errno == EINTR)
				{
				}
			}

			if (stop_token.stop_requested())
				break;

			recycle_capture_buffers();
			if (pfds[0].revents & POLLPRI)
				handle_events();
			if (capture_streaming && (pfds[0].revents & POLLIN))
				drain_capture();
			if (pfds[0].revents & POLLOUT)
				reclaim_output_buffers();
			if (pfds[0].revents & (POLLHUP | POLLNVAL))
				throw std::runtime_error("iris decoder poll returned a hangup/invalid fd");

			queue_ready_frames();
		}
	}
	catch (...)
	{
		auto error = std::current_exception();
		{
			std::scoped_lock lock(worker_exception_mutex);
			worker_exception = error;
		}
		try
		{
			std::rethrow_exception(error);
		}
		catch (const std::exception & e)
		{
			spdlog::error("V4L2 iris decoder worker failed: {}", e.what());
		}
	}
}

decoder::pending_frame decoder::take_pending(uint64_t timestamp)
{
	auto it = std::ranges::find(pending_frames, timestamp, &pending_frame::timestamp);
	if (it == pending_frames.end())
		throw std::runtime_error("iris decoder returned a frame with an unknown timestamp");
	pending_frame result = *it;
	pending_frames.erase(it);
	return result;
}

void decoder::submit_capture(unsigned int capture_index, uint64_t timestamp)
{
	if (capture_index >= capture_buffers.size())
		throw std::runtime_error("Invalid iris CAPTURE buffer index");
	if (!timestamp)
	{
		queue_capture_buffer(capture_index);
		return;
	}

	pending_frame pending = take_pending(timestamp);
	auto & capture = capture_buffers[capture_index];
	capture.held_by_vulkan = true;

	auto handle = std::make_shared<v4l2_blit_handle>(
	        pending.feedback, pending.view_info, *capture.image_view, *capture.image, vk::Extent2D{capture_width, capture_height}, capture.current_layout, capture_index, this);
	if (auto scene = weak_scene.lock())
		scene->push_blit_handle(accumulator, std::move(handle));
}

void decoder::drain_capture()
{
	while (true)
	{
		std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
		v4l2_buffer buf = make_v4l2_buffer(capture_type, 0, planes);
		if (xioctl(fd.get_fd(), VIDIOC_DQBUF, &buf) < 0)
		{
			if (errno == EAGAIN || errno == EPIPE)
				return;
			throw std::runtime_error("VIDIOC_DQBUF(CAPTURE) failed: " + std::string(std::strerror(errno)));
		}

		const uint64_t timestamp = timestamp_to_u64(buf.timestamp);
		if (!(buf.flags & V4L2_BUF_FLAG_ERROR))
		{
			submit_capture(buf.index, timestamp);
		}
		else
		{
			spdlog::warn("iris decoder returned CAPTURE buffer {} with V4L2_BUF_FLAG_ERROR", buf.index);
			if (timestamp)
			{
				auto it = std::ranges::find(pending_frames, timestamp, &pending_frame::timestamp);
				if (it != pending_frames.end())
					pending_frames.erase(it);
			}
			queue_capture_buffer(buf.index);
		}
	}
}

void decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	if (dropped_frame_index && *dropped_frame_index != frame_index)
		dropped_frame_index.reset();

	if (assembling && assembling->frame_index != frame_index)
		abandon_assembling_frame();

	if (!assembling)
	{
		if (dropped_frame_index && *dropped_frame_index == frame_index)
			return;

		std::scoped_lock lock(output_mutex);
		if (free_output_buffers.empty())
		{
			spdlog::warn("No free OUTPUT buffer for frame {}; dropping", frame_index);
			dropped_frame_index = frame_index;
			return;
		}
		const unsigned int index = free_output_buffers.back();
		free_output_buffers.pop_back();
		assembling = {.buffer_index = index, .bytes_used = 0, .frame_index = frame_index};
	}

	for (const auto & shard: data)
	{
		if (assembling->bytes_used + shard.size() > output_buffers[assembling->buffer_index].planes[0].length)
			throw std::runtime_error("Encoded frame exceeds iris OUTPUT buffer size");

		auto & mapped = output_buffers[assembling->buffer_index].planes[0];
		std::memcpy(static_cast<uint8_t *>(mapped.address) + assembling->bytes_used, shard.data(), shard.size());
		assembling->bytes_used += shard.size();
	}
	(void)partial;
}

void decoder::abandon_assembling_frame()
{
	if (!assembling.has_value())
		return;
	{
		std::scoped_lock lock(output_mutex);
		free_output_buffers.push_back(assembling->buffer_index);
	}
	assembling.reset();
}

void decoder::frame_completed(const wivrn::from_headset::feedback & feedback,
                              const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info)
{
	rethrow_worker_exception();
	if (dropped_frame_index && *dropped_frame_index == feedback.frame_index)
	{
		dropped_frame_index.reset();
		return;
	}
	if (dropped_frame_index)
		dropped_frame_index.reset();

	if (!assembling.has_value())
	{
		spdlog::warn("frame_completed() while not assembling frame");
		return;
	}
	if (assembling->frame_index != feedback.frame_index)
	{
		spdlog::warn("frame {} completed while assembling frame {}", feedback.frame_index, assembling->frame_index);
		return;
	}

	ready_output ready;
	ready.buffer_index = assembling->buffer_index;
	ready.bytes_used = assembling->bytes_used;
	ready.pending = {.timestamp = next_timestamp++, .feedback = feedback, .view_info = view_info};
	assembling.reset();

	{
		std::scoped_lock lock(input_mutex);
		input_queue.push_back(std::move(ready));
	}
	wake_worker();
}

bool decoder::available_for(wivrn::video_codec codec)
{
	try
	{
		find_device(codec);
	}
	catch (...)
	{
		return false;
	}
	return true;
}

void decoder::supported_codecs(std::vector<wivrn::video_codec> & result)
{
	if (available_for(wivrn::video_codec::h264))
		result.push_back(wivrn::video_codec::h264);
	if (available_for(wivrn::video_codec::h265))
		result.push_back(wivrn::video_codec::h265);
}
} // namespace wivrn::v4l2
