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

#include "video_encoder_nvenc.h"
#include "encoder/yuv_converter.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <stdexcept>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

#define NVENC_CHECK_NOENCODER(x)                                          \
	do                                                                \
	{                                                                 \
		NVENCSTATUS status = x;                                   \
		if (status != NV_ENC_SUCCESS)                             \
		{                                                         \
			U_LOG_E("%s:%d: %d", __FILE__, __LINE__, status); \
			throw std::runtime_error("TODO");                 \
		}                                                         \
	} while (0)

#define NVENC_CHECK(x)                                                                                                    \
	do                                                                                                                \
	{                                                                                                                 \
		NVENCSTATUS status = x;                                                                                   \
		if (status != NV_ENC_SUCCESS)                                                                             \
		{                                                                                                         \
			U_LOG_E("%s:%d: %d, %s", __FILE__, __LINE__, status, fn.nvEncGetLastErrorString(session_handle)); \
			throw std::runtime_error("TODO");                                                                 \
		}                                                                                                         \
	} while (0)

#define CU_CHECK(x)                                                                               \
	do                                                                                        \
	{                                                                                         \
		CUresult status = x;                                                              \
		if (status != CUDA_SUCCESS)                                                       \
		{                                                                                 \
			const char * error_string;                                                \
			cuGetErrorString(status, &error_string);                                  \
			U_LOG_E("%s:%d: %s (%d)", __FILE__, __LINE__, error_string, (int)status); \
			throw std::runtime_error("TODO");                                         \
		}                                                                                 \
	} while (0)

namespace xrt::drivers::wivrn
{

VideoEncoderNvenc::VideoEncoderNvenc(wivrn_vk_bundle & vk, const encoder_settings & settings, float fps) :
        vk(vk), fps(fps), bitrate(settings.bitrate)
{
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
	pitch = settings.video_width;
	CU_CHECK(cuInit(0));

	CU_CHECK(cuCtxCreate(&cuda, 0, 0));

	fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;
	NVENC_CHECK_NOENCODER(NvEncodeAPICreateInstance(&fn));

	{
		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params{
		        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
		        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
		        .device = cuda,
		        .reserved = {},
		        .apiVersion = NVENCAPI_VERSION,
		        .reserved1 = {},
		        .reserved2 = {},
		};

		NVENC_CHECK_NOENCODER(fn.nvEncOpenEncodeSessionEx(&params, &session_handle));
	}

	uint32_t count;
	std::vector<GUID> presets;
	auto encodeGUID = settings.codec == video_codec::h264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
	NVENC_CHECK(fn.nvEncGetEncodePresetCount(session_handle, encodeGUID, &count));
	presets.resize(count);
	NVENC_CHECK(fn.nvEncGetEncodePresetGUIDs(session_handle, encodeGUID, presets.data(), count, &count));

	switch (settings.codec)
	{
		case video_codec::h264:
			printf("%d H264 presets\n", count);
			break;

		case video_codec::h265:
			printf("%d HEVC presets\n", count);
			break;
	}

	for (GUID & i: presets)
	{
		printf("  Preset {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", i.Data1, i.Data2, i.Data3, i.Data4[0], i.Data4[1], i.Data4[2], i.Data4[3], i.Data4[4], i.Data4[5], i.Data4[6], i.Data4[7]);
	}

	NV_ENC_CAPS_PARAM cap_param{};
	cap_param.version = NV_ENC_CAPS_PARAM_VER;
	cap_param.capsToQuery = NV_ENC_CAPS_SUPPORT_REF_PIC_INVALIDATION;
	int cap_value;
	NVENC_CHECK(fn.nvEncGetEncodeCaps(session_handle, encodeGUID, &cap_param, &cap_value));

	// auto presetGUID = codec == video_codec::h264 ? NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID :
	// NV_ENC_PRESET_P7_GUID;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	auto presetGUID = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
#pragma GCC diagnostic pop
	NV_ENC_PRESET_CONFIG preset_config;
	preset_config.version = NV_ENC_PRESET_CONFIG_VER;
	preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
	NVENC_CHECK(fn.nvEncGetEncodePresetConfig(session_handle, encodeGUID, presetGUID, &preset_config));

	NV_ENC_CONFIG params = preset_config.presetCfg;

	// Bitrate control
	params.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
	params.rcParams.averageBitRate = bitrate;
	params.rcParams.maxBitRate = bitrate;
	params.rcParams.vbvBufferSize = bitrate / fps;
	params.rcParams.vbvInitialDelay = bitrate / fps;

	params.gopLength = NVENC_INFINITE_GOPLENGTH;
	params.frameIntervalP = 1;

	switch (settings.codec)
	{
		case video_codec::h264:
			params.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
			params.encodeCodecConfig.h264Config.maxNumRefFrames = 0;
			params.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			break;
		case video_codec::h265:
			params.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
			params.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 0;
			params.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			break;
	}

	NV_ENC_INITIALIZE_PARAMS params2{};
	params2.version = NV_ENC_INITIALIZE_PARAMS_VER;
	params2.encodeGUID = encodeGUID;
	params2.presetGUID = presetGUID;
	params2.encodeWidth = settings.video_width;
	params2.encodeHeight = settings.video_height;
	params2.darWidth = settings.video_width;
	params2.darHeight = settings.video_height;
	params2.frameRateNum = (uint32_t)fps;
	params2.frameRateDen = 1;
	params2.enableEncodeAsync = 0;
	params2.enablePTD = 1;
	params2.reportSliceOffsets = 0;
	params2.enableSubFrameWrite = 0;
	params2.enableExternalMEHints = 0;
	params2.enableMEOnlyMode = 0;
	params2.enableWeightedPrediction = 0;
	params2.enableOutputInVidmem = 0;
	params2.encodeConfig = &params;
	params2.maxEncodeWidth = 0;
	params2.maxEncodeHeight = 0;
	params2.bufferFormat = NV_ENC_BUFFER_FORMAT_UNDEFINED;
	NVENC_CHECK(fn.nvEncInitializeEncoder(session_handle, &params2));

	NV_ENC_CREATE_BITSTREAM_BUFFER params3;
	params3.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
	NVENC_CHECK(fn.nvEncCreateBitstreamBuffer(session_handle, &params3));
	bitstreamBuffer = params3.bitstreamBuffer;

	vk::DeviceSize buffer_size = pitch * settings.video_height * 3 / 2;

	vk::StructureChain buffer_create_info{
	        vk::BufferCreateInfo{
	                .size = buffer_size,
	                .usage = vk::BufferUsageFlagBits::eTransferDst,
	        },
	        vk::ExternalMemoryBufferCreateInfo{
	                .handleTypes = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
	        },
	};

	yuv_buffer = vk::raii::Buffer(vk.device, buffer_create_info.get());
	auto memory_req = yuv_buffer.getMemoryRequirements();

	vk::StructureChain mem_info{
	        vk::MemoryAllocateInfo{
	                .allocationSize = buffer_size,
	                .memoryTypeIndex = vk.get_memory_type(memory_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
	        },
	        vk::MemoryDedicatedAllocateInfo{
	                .buffer = *yuv_buffer,
	        },
	        vk::ExportMemoryAllocateInfo{
	                .handleTypes = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
	        },
	};
	mem = vk.device.allocateMemory(mem_info.get());
	yuv_buffer.bindMemory(*mem, 0);

	int fd = vk.device.getMemoryFdKHR({
	        .memory = *mem,
	        .handleType = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
	});

	{
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC param{
		        .type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD,
		        .handle = {.fd = fd},
		        .size = memory_req.size,
		        .flags = 0,
		};
		CU_CHECK(cuImportExternalMemory(&extmem, &param));

		CUDA_EXTERNAL_MEMORY_BUFFER_DESC map_param{
		        .offset = 0,
		        .size = buffer_size,
		        .flags = 0,
		};
		CU_CHECK(cuExternalMemoryGetMappedBuffer(&frame, extmem, &map_param));
	}

	NV_ENC_REGISTER_RESOURCE param3{};
	param3.version = NV_ENC_REGISTER_RESOURCE_VER;
	param3.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
	param3.width = rect.extent.width;
	param3.height = rect.extent.height;
	param3.pitch = pitch;
	param3.resourceToRegister = (void *)frame;
	param3.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
	param3.bufferUsage = NV_ENC_INPUT_IMAGE;
	NVENC_CHECK(fn.nvEncRegisterResource(session_handle, &param3));
	nvenc_resource = param3.registeredResource;
	CU_CHECK(cuCtxPopCurrent(NULL));
}

void VideoEncoderNvenc::PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf)
{
	cmd_buf.copyImageToBuffer(
	        src_yuv.luma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *yuv_buffer,
	        vk::BufferImageCopy{
	                .bufferRowLength = uint32_t(pitch),
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
	        *yuv_buffer,
	        vk::BufferImageCopy{
	                .bufferOffset = pitch * rect.extent.height,
	                .bufferRowLength = uint32_t(pitch/2),
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
	return;
}

void VideoEncoderNvenc::Encode(bool idr, std::chrono::steady_clock::time_point pts)
{
	CU_CHECK(cuCtxPushCurrent(cuda));

	NV_ENC_MAP_INPUT_RESOURCE param4{};
	param4.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
	param4.registeredResource = nvenc_resource;
	NVENC_CHECK(fn.nvEncMapInputResource(session_handle, &param4));

	NV_ENC_PIC_PARAMS param{};
	param.version = NV_ENC_PIC_PARAMS_VER;
	param.inputWidth = rect.extent.width;
	param.inputHeight = rect.extent.height;
	param.inputPitch = pitch;
	param.encodePicFlags = (uint32_t)(idr ? NV_ENC_PIC_TYPE_IDR : NV_ENC_PIC_TYPE_P);
	param.frameIdx = 0;
	param.inputTimeStamp = 0;
	param.inputBuffer = param4.mappedResource;
	param.outputBitstream = bitstreamBuffer;
	param.bufferFmt = param4.mappedBufferFmt;
	param.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
	NVENC_CHECK(fn.nvEncEncodePicture(session_handle, &param));

	NV_ENC_LOCK_BITSTREAM param2{};
	param2.version = NV_ENC_LOCK_BITSTREAM_VER;
	param2.doNotWait = 0;
	param2.outputBitstream = bitstreamBuffer;
	NVENC_CHECK(fn.nvEncLockBitstream(session_handle, &param2));

	SendData(
	        {(uint8_t *)param2.bitstreamBufferPtr, (uint8_t *)param2.bitstreamBufferPtr + param2.bitstreamSizeInBytes}, true);

	NVENC_CHECK(fn.nvEncUnlockBitstream(session_handle, bitstreamBuffer));

	CU_CHECK(cuCtxPopCurrent(NULL));
}

} // namespace xrt::drivers::wivrn
