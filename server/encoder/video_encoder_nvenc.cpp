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

#include "encoder_settings.h"

#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <stdexcept>

#define NVENC_CHECK_NOENCODER(x)                                          \
	do                                                                \
	{                                                                 \
		NVENCSTATUS status = x;                                   \
		if (status != NV_ENC_SUCCESS)                             \
		{                                                         \
			U_LOG_E("%s:%d: %d", __FILE__, __LINE__, status); \
			throw std::runtime_error("nvenc error");          \
		}                                                         \
	} while (0)

#define NVENC_CHECK(x)                                                                                                                  \
	do                                                                                                                              \
	{                                                                                                                               \
		NVENCSTATUS status = x;                                                                                                 \
		if (status != NV_ENC_SUCCESS)                                                                                           \
		{                                                                                                                       \
			U_LOG_E("%s:%d: %d, %s", __FILE__, __LINE__, status, shared_state->fn.nvEncGetLastErrorString(session_handle)); \
			throw std::runtime_error("nvenc error");                                                                        \
		}                                                                                                                       \
	} while (0)

#define CU_CHECK(x)                                                                               \
	do                                                                                        \
	{                                                                                         \
		CUresult status = x;                                                              \
		if (status != CUDA_SUCCESS)                                                       \
		{                                                                                 \
			const char * error_string;                                                \
			shared_state->cuda_fn->cuGetErrorString(status, &error_string);           \
			U_LOG_E("%s:%d: %s (%d)", __FILE__, __LINE__, error_string, (int)status); \
			throw std::runtime_error(std::string("CUDA error: ") + error_string);     \
		}                                                                                 \
	} while (0)

namespace wivrn
{

static auto encode_guid(video_codec codec)
{
	switch (codec)
	{
		case h264:
			return NV_ENC_CODEC_H264_GUID;
		case h265:
			return NV_ENC_CODEC_HEVC_GUID;
		case av1:
			return NV_ENC_CODEC_AV1_GUID;
	}
	throw std::out_of_range("Invalid codec " + std::to_string(codec));
}

video_encoder_nvenc::video_encoder_nvenc(
        wivrn_vk_bundle & vk,
        encoder_settings & settings,
        float fps,
        uint8_t stream_idx) :
        video_encoder(stream_idx, settings.channels, settings.bitrate_multiplier, true),
        vk(vk),
        shared_state(video_encoder_nvenc_shared_state::get()),
        fps(fps),
        bitrate(settings.bitrate)
{
	if (settings.bit_depth != 8)
		throw std::runtime_error("nvenc encoder only supports 8-bit encoding");

	assert(settings.width % 32 == 0);
	assert(settings.height % 32 == 0);
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

	auto encodeGUID = encode_guid(settings.codec);
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params_ex = {
	        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
	        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
	        .device = shared_state->cuda,
	        .apiVersion = NVENCAPI_VERSION,
	};
	NVENC_CHECK_NOENCODER(shared_state->fn.nvEncOpenEncodeSessionEx(&params_ex, &session_handle));

	uint32_t count;
	std::vector<GUID> presets;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetCount(session_handle, encodeGUID, &count));
	presets.resize(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetGUIDs(session_handle, encodeGUID, presets.data(), count, &count));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	auto presetGUID = NV_ENC_PRESET_P4_GUID;
	NV_ENC_TUNING_INFO tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
#pragma GCC diagnostic pop
	NV_ENC_PRESET_CONFIG preset_config{
	        .version = NV_ENC_PRESET_CONFIG_VER,
	        .presetCfg = {
	                .version = NV_ENC_CONFIG_VER,
	        }};
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetConfigEx(session_handle, encodeGUID, presetGUID, tuningInfo, &preset_config));

	NV_ENC_CONFIG params = preset_config.presetCfg;

	// Bitrate control
	params.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
	params.rcParams.averageBitRate = bitrate;
	params.rcParams.maxBitRate = bitrate;
	params.rcParams.vbvBufferSize = bitrate / fps;
	params.rcParams.vbvInitialDelay = bitrate / fps;
	params.rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;

	params.gopLength = NVENC_INFINITE_GOPLENGTH;
	params.frameIntervalP = 1;

	switch (settings.codec)
	{
		case video_codec::h264:
			params.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
			params.encodeCodecConfig.h264Config.maxNumRefFrames = 0;
			params.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			params.encodeCodecConfig.h264Config.h264VUIParameters.videoFullRangeFlag = 1;
			break;
		case video_codec::h265:
			params.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
			params.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 0;
			params.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			params.encodeCodecConfig.hevcConfig.hevcVUIParameters.videoFullRangeFlag = 1;
			break;
		case video_codec::av1:
			params.encodeCodecConfig.av1Config.repeatSeqHdr = 1;
			params.encodeCodecConfig.av1Config.maxNumRefFramesInDPB = 0;
			params.encodeCodecConfig.av1Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			break;
	}

	NV_ENC_INITIALIZE_PARAMS params2{
	        .version = NV_ENC_INITIALIZE_PARAMS_VER,
	        .encodeGUID = encodeGUID,
	        .presetGUID = presetGUID,
	        .encodeWidth = settings.video_width,
	        .encodeHeight = settings.video_height,
	        .darWidth = settings.video_width,
	        .darHeight = settings.video_height,
	        .frameRateNum = (uint32_t)fps,
	        .frameRateDen = 1,
	        .enableEncodeAsync = 0,
	        .enablePTD = 1,
	        .encodeConfig = &params,
	        .tuningInfo = tuningInfo};
	NVENC_CHECK(shared_state->fn.nvEncInitializeEncoder(session_handle, &params2));

	NV_ENC_CREATE_BITSTREAM_BUFFER params3{
	        .version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER,
	};
	NVENC_CHECK(shared_state->fn.nvEncCreateBitstreamBuffer(session_handle, &params3));
	bitstreamBuffer = params3.bitstreamBuffer;

	vk::DeviceSize buffer_size = rect.extent.width * settings.video_height * 3 / 2;

	vk::StructureChain buffer_create_info{
	        vk::BufferCreateInfo{
	                .size = buffer_size,
	                .usage = vk::BufferUsageFlagBits::eTransferDst,
	        },
	        vk::ExternalMemoryBufferCreateInfo{
	                .handleTypes = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
	        },
	};

	for (auto & i: in)
	{
		i.yuv = vk::raii::Buffer(vk.device, buffer_create_info.get());
		vk.name(i.yuv, "nvenc yuv buffer");
		auto memory_req = i.yuv.getMemoryRequirements();

		vk::StructureChain mem_info{
		        vk::MemoryAllocateInfo{
		                .allocationSize = buffer_size,
		                .memoryTypeIndex = vk.get_memory_type(memory_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
		        },
		        vk::MemoryDedicatedAllocateInfo{
		                .buffer = *i.yuv,
		        },
		        vk::ExportMemoryAllocateInfo{
		                .handleTypes = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
		        },
		};
		i.mem = vk.device.allocateMemory(mem_info.get());
		vk.name(i.mem, "nvenc memory");
		i.yuv.bindMemory(*i.mem, 0);

		int fd = vk.device.getMemoryFdKHR({
		        .memory = *i.mem,
		        .handleType = vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd,
		});

		CUdeviceptr frame;
		CU_CHECK(shared_state->cuda_fn->cuCtxPushCurrent(shared_state->cuda));
		{
			CUexternalMemory extmem;
			CUDA_EXTERNAL_MEMORY_HANDLE_DESC param{
			        .type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD,
			        .handle = {.fd = fd},
			        .size = memory_req.size,
			        .flags = 0,
			};
			CU_CHECK(shared_state->cuda_fn->cuImportExternalMemory(&extmem, &param));

			CUDA_EXTERNAL_MEMORY_BUFFER_DESC map_param{
			        .offset = 0,
			        .size = buffer_size,
			        .flags = 0,
			};
			CU_CHECK(shared_state->cuda_fn->cuExternalMemoryGetMappedBuffer(&frame, extmem, &map_param));
		}

		NV_ENC_REGISTER_RESOURCE param3{
		        .version = NV_ENC_REGISTER_RESOURCE_VER,
		        .resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
		        .width = settings.video_width,
		        .height = settings.video_height,
		        .pitch = rect.extent.width,
		        .resourceToRegister = (void *)frame,
		        .bufferFormat = NV_ENC_BUFFER_FORMAT_NV12,
		        .bufferUsage = NV_ENC_INPUT_IMAGE,
		};
		NVENC_CHECK(shared_state->fn.nvEncRegisterResource(session_handle, &param3));
		i.nvenc_resource = param3.registeredResource;
	}
	CU_CHECK(shared_state->cuda_fn->cuCtxPopCurrent(NULL));
}

video_encoder_nvenc::~video_encoder_nvenc()
{
	if (session_handle)
		shared_state->fn.nvEncDestroyEncoder(session_handle);
}

std::pair<bool, vk::Semaphore> video_encoder_nvenc::present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t)
{
	cmd_buf.copyImageToBuffer(
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *in[slot].yuv,
	        vk::BufferImageCopy{
	                .bufferRowLength = rect.extent.width,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .baseArrayLayer = uint32_t(channels),
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
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        *in[slot].yuv,
	        vk::BufferImageCopy{
	                .bufferOffset = rect.extent.width * rect.extent.height,
	                .bufferRowLength = uint32_t(rect.extent.width / 2),
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .baseArrayLayer = uint32_t(channels),
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
	return {false, nullptr};
}

std::optional<video_encoder::data> video_encoder_nvenc::encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot)
{
	CU_CHECK(shared_state->cuda_fn->cuCtxPushCurrent(shared_state->cuda));

	NV_ENC_MAP_INPUT_RESOURCE param4{};
	param4.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
	param4.registeredResource = in[slot].nvenc_resource;
	NVENC_CHECK(shared_state->fn.nvEncMapInputResource(session_handle, &param4));

	NV_ENC_PIC_PARAMS param{
	        .version = NV_ENC_PIC_PARAMS_VER,
	        .inputWidth = rect.extent.width,
	        .inputHeight = rect.extent.height,
	        .inputPitch = rect.extent.width,
	        .encodePicFlags = uint32_t(idr ? NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS : 0),
	        .frameIdx = 0,
	        .inputTimeStamp = 0,
	        .inputBuffer = param4.mappedResource,
	        .outputBitstream = bitstreamBuffer,
	        .bufferFmt = param4.mappedBufferFmt,
	        .pictureStruct = NV_ENC_PIC_STRUCT_FRAME,
	};
	NVENC_CHECK(shared_state->fn.nvEncEncodePicture(session_handle, &param));

	NV_ENC_LOCK_BITSTREAM param2{
	        .version = NV_ENC_LOCK_BITSTREAM_VER,
	        .doNotWait = 0,
	        .outputBitstream = bitstreamBuffer,
	};
	NVENC_CHECK(shared_state->fn.nvEncLockBitstream(session_handle, &param2));

	CU_CHECK(shared_state->cuda_fn->cuCtxPopCurrent(NULL));
	return data{
	        .encoder = this,
	        .span = std::span((uint8_t *)param2.bitstreamBufferPtr, param2.bitstreamSizeInBytes),
	        .mem = std::shared_ptr<void>(param2.bitstreamBufferPtr, [this](void *) {
		        NVENCSTATUS status = shared_state->fn.nvEncUnlockBitstream(session_handle, bitstreamBuffer);
		        if (status != NV_ENC_SUCCESS)
			        U_LOG_E("%s:%d: %d, %s", __FILE__, __LINE__, status, shared_state->fn.nvEncGetLastErrorString(session_handle));
	        }),
	};
}

std::array<int, 2> video_encoder_nvenc::get_max_size(video_codec codec)
{
	video_encoder_nvenc_shared_state state;
	void * session_handle = nullptr;
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params_ex = {
	        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
	        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
	        .device = state.cuda,
	        .apiVersion = NVENCAPI_VERSION,
	};
	if (state.fn.nvEncOpenEncodeSessionEx(&params_ex, &session_handle) != NV_ENC_SUCCESS)
	{
		throw std::runtime_error("nvenc get_max_size: Failed to open session");
	}
	std::array<int, 2> result;
	std::exception_ptr ex;
	try
	{
		auto encodeGUID = encode_guid(codec);
		for (auto [cap, res]: {
		             std::pair{NV_ENC_CAPS_WIDTH_MAX, &result[0]},
		             {NV_ENC_CAPS_WIDTH_MAX, &result[1]},
		     })
		{
			NV_ENC_CAPS_PARAM cap_param{
			        .version = NV_ENC_CAPS_PARAM_VER,
			        .capsToQuery = NV_ENC_CAPS_WIDTH_MAX,
			};
			NVENCSTATUS status = state.fn.nvEncGetEncodeCaps(session_handle, encodeGUID, &cap_param, res);
			if (status != NV_ENC_SUCCESS)
			{
				throw std::runtime_error("nvenc get_max_size: failed to get caps");
			}
		}
	}
	catch (...)
	{
		ex = std::current_exception();
	}
	if (session_handle)
	{
		state.fn.nvEncDestroyEncoder(session_handle);
	}
	if (ex)
		std::rethrow_exception(ex);
	U_LOG_D("nvenc maximum encoded size: %dx%d", result[0], result[1]);
	return result;
}

} // namespace wivrn
