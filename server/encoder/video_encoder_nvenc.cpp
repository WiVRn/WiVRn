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

bool operator==(const GUID & l, const GUID & r)
{
	return l.Data1 == r.Data1 and
	       l.Data2 == r.Data2 and
	       l.Data3 == r.Data3 and
	       std::ranges::equal(l.Data4, r.Data4);
}

#include <algorithm>
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
		case raw:
			break;
	}
	throw std::out_of_range("Invalid codec " + std::to_string(codec));
}

static void check_encode_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid)
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeGUIDCount(session_handle, &count));

	std::vector<GUID> encodeGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeGUIDs(session_handle, encodeGUIDs.data(), count, &count));

	if (!std::ranges::contains(encodeGUIDs, encode_guid))
	{
		throw std::runtime_error("nvenc: GPU doesn't support selected codec.");
	}
}

static void check_preset_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid, GUID preset_guid)
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetCount(session_handle, encode_guid, &count));

	std::vector<GUID> presetGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetGUIDs(session_handle, encode_guid, presetGUIDs.data(), count, &count));

	if (!std::ranges::contains(presetGUIDs, preset_guid))
	{
		throw std::runtime_error("nvenc: Internal error. GPU doesn't support selected encoder preset.");
	}
}

static void check_profile_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encodeGUID, GUID profileGUID, std::string err_msg = "GPU doesn't support selected encoding profile.")
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeProfileGUIDCount(session_handle, encodeGUID, &count));

	std::vector<GUID> profileGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeProfileGUIDs(session_handle, encodeGUID, profileGUIDs.data(), count, &count));

	if (!std::ranges::contains(profileGUIDs, profileGUID))
	{
		throw std::runtime_error("nvenc: " + err_msg);
	}
}

NV_ENC_RC_PARAMS video_encoder_nvenc::get_rc_params(uint64_t bitrate, float framerate)
{
	return {
	        .rateControlMode = NV_ENC_PARAMS_RC_CBR,
	        .averageBitRate = static_cast<uint32_t>(bitrate),
	        .vbvBufferSize = static_cast<uint32_t>(bitrate / framerate),
	        .vbvInitialDelay = static_cast<uint32_t>(bitrate / framerate),
	        .multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION};
}

void video_encoder_nvenc::set_init_params_fps(float framerate)
{
	init_params.frameRateNum = framerate;
	init_params.frameRateDen = 1;
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
	if (settings.bit_depth != 8 && settings.bit_depth != 10)
		throw std::runtime_error("nvenc encoder only supports 8-bit and 10-bit encoding");

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

	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = {
	        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
	        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
	        .device = shared_state->cuda,
	        .apiVersion = NVENCAPI_VERSION,
	};
	NVENC_CHECK_NOENCODER(shared_state->fn.nvEncOpenEncodeSessionEx(&session_params, &session_handle));

	auto encodeGUID = encode_guid(settings.codec);
	check_encode_guid_supported(shared_state, session_handle, encodeGUID);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	GUID presetGUID = NV_ENC_PRESET_P4_GUID;
	check_preset_guid_supported(shared_state, session_handle, encodeGUID, presetGUID);

	NV_ENC_TUNING_INFO tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
#pragma GCC diagnostic pop
	NV_ENC_PRESET_CONFIG preset_config{
	        .version = NV_ENC_PRESET_CONFIG_VER,
	        .presetCfg = {
	                .version = NV_ENC_CONFIG_VER,
	        }};
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetConfigEx(session_handle, encodeGUID, presetGUID, tuningInfo, &preset_config));

	config = preset_config.presetCfg;

	// Bitrate control
	config.rcParams = get_rc_params(bitrate, fps);

	config.gopLength = NVENC_INFINITE_GOPLENGTH;
	config.frameIntervalP = 1;

	NV_ENC_BIT_DEPTH bitDepth = NV_ENC_BIT_DEPTH_8;
	if (settings.bit_depth == 10)
	{
		NV_ENC_CAPS_PARAM cap_param{
		        .version = NV_ENC_CAPS_PARAM_VER,
		        .capsToQuery = NV_ENC_CAPS_SUPPORT_10BIT_ENCODE,
		};

		int res = 0;
		NVENC_CHECK(shared_state->fn.nvEncGetEncodeCaps(session_handle, encodeGUID, &cap_param, &res));

		if (res == 1)
		{
			bitDepth = NV_ENC_BIT_DEPTH_10;
			bytesPerPixel = 2;
		}
		else
		{
			throw std::runtime_error("nvenc: 10-bit encoding requested, but GPU doesn't support it");
		}
	}

	switch (settings.codec)
	{
		case video_codec::h264:
			if (bitDepth != NV_ENC_BIT_DEPTH_8)
				throw std::runtime_error("nvenc: selected codec only supports 8-bit encoding");

			config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
			config.encodeCodecConfig.h264Config.maxNumRefFrames = 0;
			config.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			config.encodeCodecConfig.h264Config.h264VUIParameters.videoFullRangeFlag = 1;

			break;
		case video_codec::h265:
			if (bitDepth == NV_ENC_BIT_DEPTH_10)
			{
				config.profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
				check_profile_guid_supported(shared_state, session_handle, encodeGUID, config.profileGUID, "GPU doesn't support 10-bit depth with H.265 codec.");
			}

			config.encodeCodecConfig.hevcConfig.inputBitDepth = bitDepth;
			config.encodeCodecConfig.hevcConfig.outputBitDepth = bitDepth;

			config.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
			config.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 0;
			config.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
			config.encodeCodecConfig.hevcConfig.hevcVUIParameters.videoFullRangeFlag = 1;

			break;
		case video_codec::av1:
			if (bitDepth == NV_ENC_BIT_DEPTH_10)
			{
				config.profileGUID = NV_ENC_AV1_PROFILE_MAIN_GUID;
				check_profile_guid_supported(shared_state, session_handle, encodeGUID, config.profileGUID, "GPU doesn't support 10-bit depth with AV1 codec.");
			}

			config.encodeCodecConfig.av1Config.inputBitDepth = bitDepth;
			config.encodeCodecConfig.av1Config.outputBitDepth = bitDepth;

			config.encodeCodecConfig.av1Config.repeatSeqHdr = 1;
			config.encodeCodecConfig.av1Config.maxNumRefFramesInDPB = 0;
			config.encodeCodecConfig.av1Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;

			break;
		case video_codec::raw:
			throw std::runtime_error("raw codec not supported for nvenc");
	}

	init_params = {
	        .version = NV_ENC_INITIALIZE_PARAMS_VER,
	        .encodeGUID = encodeGUID,
	        .presetGUID = presetGUID,
	        .encodeWidth = settings.video_width,
	        .encodeHeight = settings.video_height,
	        .darWidth = settings.video_width,
	        .darHeight = settings.video_height,
	        .enableEncodeAsync = 0,
	        .enablePTD = 1,
	        .encodeConfig = &config,
	        .tuningInfo = tuningInfo};

	set_init_params_fps(fps);

	NVENC_CHECK(shared_state->fn.nvEncInitializeEncoder(session_handle, &init_params));

	NV_ENC_CREATE_BITSTREAM_BUFFER out_buf_params{
	        .version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER,
	};
	NVENC_CHECK(shared_state->fn.nvEncCreateBitstreamBuffer(session_handle, &out_buf_params));
	outputBuffer = out_buf_params.bitstreamBuffer;

	vk::DeviceSize buffer_size = rect.extent.width * settings.video_height * bytesPerPixel * 3 / 2;

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
			CUDA_EXTERNAL_MEMORY_HANDLE_DESC mem_handle_params{
			        .type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD,
			        .handle = {.fd = fd},
			        .size = memory_req.size,
			        .flags = 0,
			};
			CU_CHECK(shared_state->cuda_fn->cuImportExternalMemory(&extmem, &mem_handle_params));

			CUDA_EXTERNAL_MEMORY_BUFFER_DESC ext_map_params{
			        .offset = 0,
			        .size = buffer_size,
			        .flags = 0,
			};
			CU_CHECK(shared_state->cuda_fn->cuExternalMemoryGetMappedBuffer(&frame, extmem, &ext_map_params));
		}

		NV_ENC_REGISTER_RESOURCE resource_params{
		        .version = NV_ENC_REGISTER_RESOURCE_VER,
		        .resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
		        .width = settings.video_width,
		        .height = settings.video_height,
		        .pitch = rect.extent.width * bytesPerPixel,
		        .resourceToRegister = (void *)frame,
		        .bufferFormat = (bitDepth == NV_ENC_BIT_DEPTH_10 ? NV_ENC_BUFFER_FORMAT_YUV420_10BIT : NV_ENC_BUFFER_FORMAT_NV12),
		        .bufferUsage = NV_ENC_INPUT_IMAGE,
		};
		NVENC_CHECK(shared_state->fn.nvEncRegisterResource(session_handle, &resource_params));
		i.nvenc_resource = resource_params.registeredResource;
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
	        std::array{
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
	                        }},
	                vk::BufferImageCopy{.bufferOffset = rect.extent.width * rect.extent.height * bytesPerPixel, .bufferRowLength = uint32_t(rect.extent.width / 2), .imageSubresource = {
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
	                                    }}});

	return {false, nullptr};
}

std::optional<video_encoder::data> video_encoder_nvenc::encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot)
{
	CU_CHECK(shared_state->cuda_fn->cuCtxPushCurrent(shared_state->cuda));

	auto new_bitrate = pending_bitrate.exchange(0);
	auto new_framerate = pending_framerate.exchange(0);

	if (new_bitrate || new_framerate)
	{
		if (new_framerate)
			U_LOG_I("nvenc: reconfiguring framerate, new value: %f", new_framerate);
		else
			new_framerate = fps;

		if (new_bitrate)
			U_LOG_I("nvenc: reconfiguring bitrate, new value: %d", new_bitrate);
		else
			new_bitrate = bitrate;

		config.rcParams = get_rc_params(new_bitrate, new_framerate);
		set_init_params_fps(new_framerate);

		NV_ENC_RECONFIGURE_PARAMS reconfig_params{
		        .version = NV_ENC_RECONFIGURE_PARAMS_VER,
		        .reInitEncodeParams = init_params,
		        .resetEncoder = 1,
		        .forceIDR = 1};

		try
		{
			NVENC_CHECK(shared_state->fn.nvEncReconfigureEncoder(session_handle, &reconfig_params));
			fps = new_framerate;
			bitrate = new_bitrate;
			idr = true;

			U_LOG_I("nvenc: reconfiguring succeeded.");
		}
		catch (const std::exception & e)
		{
			U_LOG_E("nvenc: reconfiguring failed.");
			config.rcParams = get_rc_params(bitrate, fps);
			set_init_params_fps(fps);
		}
	}

	NV_ENC_MAP_INPUT_RESOURCE inp_resource_params{
	        .version = NV_ENC_MAP_INPUT_RESOURCE_VER,
	        .registeredResource = in[slot].nvenc_resource};

	NVENC_CHECK(shared_state->fn.nvEncMapInputResource(session_handle, &inp_resource_params));

	NV_ENC_PIC_PARAMS frame_params{
	        .version = NV_ENC_PIC_PARAMS_VER,
	        .inputWidth = rect.extent.width,
	        .inputHeight = rect.extent.height,
	        .inputPitch = rect.extent.width,
	        .encodePicFlags = uint32_t(idr ? NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS : 0),
	        .frameIdx = 0,
	        .inputTimeStamp = 0,
	        .inputBuffer = inp_resource_params.mappedResource,
	        .outputBitstream = outputBuffer,
	        .bufferFmt = inp_resource_params.mappedBufferFmt,
	        .pictureStruct = NV_ENC_PIC_STRUCT_FRAME,
	};
	NVENC_CHECK(shared_state->fn.nvEncEncodePicture(session_handle, &frame_params));

	NV_ENC_LOCK_BITSTREAM buf_lock_params{
	        .version = NV_ENC_LOCK_BITSTREAM_VER,
	        .doNotWait = 0,
	        .outputBitstream = outputBuffer,
	};
	NVENC_CHECK(shared_state->fn.nvEncLockBitstream(session_handle, &buf_lock_params));

	CU_CHECK(shared_state->cuda_fn->cuCtxPopCurrent(NULL));
	return data{
	        .encoder = this,
	        .span = std::span((uint8_t *)buf_lock_params.bitstreamBufferPtr, buf_lock_params.bitstreamSizeInBytes),
	        .mem = std::shared_ptr<void>(buf_lock_params.bitstreamBufferPtr, [this](void *) {
		        NVENCSTATUS status = shared_state->fn.nvEncUnlockBitstream(session_handle, outputBuffer);
		        if (status != NV_ENC_SUCCESS)
			        U_LOG_E("%s:%d: %d, %s", __FILE__, __LINE__, status, shared_state->fn.nvEncGetLastErrorString(session_handle));
	        }),
	};
}

std::array<int, 2> video_encoder_nvenc::get_max_size(video_codec codec)
{
	std::shared_ptr<video_encoder_nvenc_shared_state> state = video_encoder_nvenc_shared_state::get();
	void * session_handle = nullptr;
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params_ex = {
	        .version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
	        .deviceType = NV_ENC_DEVICE_TYPE_CUDA,
	        .device = state->cuda,
	        .apiVersion = NVENCAPI_VERSION,
	};
	if (state->fn.nvEncOpenEncodeSessionEx(&params_ex, &session_handle) != NV_ENC_SUCCESS)
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
			NV_ENC_CAPS_PARAM cap_params{
			        .version = NV_ENC_CAPS_PARAM_VER,
			        .capsToQuery = NV_ENC_CAPS_WIDTH_MAX,
			};

			check_encode_guid_supported(state, session_handle, encodeGUID);

			NVENCSTATUS status = state->fn.nvEncGetEncodeCaps(session_handle, encodeGUID, &cap_params, res);
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
		state->fn.nvEncDestroyEncoder(session_handle);
	}
	if (ex)
		std::rethrow_exception(ex);
	U_LOG_D("nvenc maximum encoded size: %dx%d", result[0], result[1]);
	return result;
}

} // namespace wivrn
