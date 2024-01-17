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

#include "video_encoder_nvenc.h"

#include "vk/vk_helpers.h"
#include <stdexcept>

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

VideoEncoderNvenc::VideoEncoderNvenc(vk_bundle * vk, const encoder_settings & settings, float fps) :
        vk(vk), offset_x(settings.offset_x), offset_y(settings.offset_y), width(settings.width), height(settings.height), codec(settings.codec), fps(fps), bitrate(settings.bitrate)
{
	CU_CHECK(cuInit(0));

	CU_CHECK(cuCtxCreate(&cuda, 0, 0));

	fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;
	NVENC_CHECK_NOENCODER(NvEncodeAPICreateInstance(&fn));

	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params{};
	params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
	params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
	params.device = cuda;
	params.apiVersion = NVENCAPI_VERSION;

	NVENC_CHECK_NOENCODER(fn.nvEncOpenEncodeSessionEx(&params, &session_handle));

	uint32_t count;
	std::vector<GUID> presets;
	auto encodeGUID = codec == video_codec::h264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
	NVENC_CHECK(fn.nvEncGetEncodePresetCount(session_handle, encodeGUID, &count));
	presets.resize(count);
	NVENC_CHECK(fn.nvEncGetEncodePresetGUIDs(session_handle, encodeGUID, presets.data(), count, &count));

	switch (codec)
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
	supports_frame_invalidation = cap_value;
	if (supports_frame_invalidation)
		printf("Frame invalidation supported\n");
	else
		printf("Frame invalidation not supported\n");
}

void VideoEncoderNvenc::SetImages(int full_width,
                                  int full_height,
                                  VkFormat format,
                                  int num_images,
                                  VkImage * images,
                                  VkImageView * views,
                                  VkDeviceMemory * memory)
{
	auto encodeGUID = codec == video_codec::h264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
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

	switch (codec)
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
	params2.encodeWidth = uint32_t(width);
	params2.encodeHeight = uint32_t(height);
	params2.darWidth = uint32_t(width);
	params2.darHeight = uint32_t(height);
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

	// TODO: cleanup on error
	this->images.resize(num_images);
	CU_CHECK(cuCtxPushCurrent(cuda));
	for (int i = 0; i < num_images; i++)
	{
		VkMemoryGetFdInfoKHR getinfo{};
		getinfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
		getinfo.pNext = nullptr;
		getinfo.memory = memory[i];
		getinfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

		int fd;
		VkResult res = vk->vkGetMemoryFdKHR(vk->device, &getinfo, &fd);
		VK_CHK_WITH_RET(res, "vkGetMemoryFdKHR", );

		VkMemoryRequirements memoryreq;
		vk->vkGetImageMemoryRequirements(vk->device, images[i], &memoryreq);

		CUexternalMemory extmem;
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC param{};
		param.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
		param.handle.fd = fd;
		param.size = memoryreq.size;
		param.flags = 0;
		CU_CHECK(cuImportExternalMemory(&extmem, &param));

		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC param2{};
		param2.offset = 0;
		param2.arrayDesc.Width = (size_t)full_width;
		param2.arrayDesc.Height = (size_t)full_height, param2.arrayDesc.Depth = 0;
		param2.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT32, param2.arrayDesc.NumChannels = 1;
		param2.arrayDesc.Flags = 0; // CUDA_ARRAY3D_SURFACE_LDST,
		param2.numLevels = 1;
		CU_CHECK(cuExternalMemoryGetMappedMipmappedArray(&this->images[i].cuda_image, extmem, &param2));
		CU_CHECK(cuMipmappedArrayGetLevel(&this->images[i].cuda_array, this->images[i].cuda_image, 0));
	}

	CU_CHECK(cuMemAllocPitch(&frame, &pitch, width * 4, height, 4));

	NV_ENC_REGISTER_RESOURCE param3{};
	param3.version = NV_ENC_REGISTER_RESOURCE_VER;
	param3.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
	param3.width = width;
	param3.height = height;
	param3.pitch = pitch;
	param3.resourceToRegister = (void *)frame;
	param3.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
	param3.bufferUsage = NV_ENC_INPUT_IMAGE;
	NVENC_CHECK(fn.nvEncRegisterResource(session_handle, &param3));
	nvenc_resource = param3.registeredResource;
	CU_CHECK(cuCtxPopCurrent(NULL));
}

void VideoEncoderNvenc::Encode(int index, bool idr, std::chrono::steady_clock::time_point pts)
{
	CU_CHECK(cuCtxPushCurrent(cuda));
	CUDA_MEMCPY2D copy{};
	copy.srcXInBytes = offset_x * 4;
	copy.srcY = offset_y;
	copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
	copy.srcArray = images[index].cuda_array;
	copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
	copy.dstDevice = frame;
	copy.dstPitch = pitch;
	copy.WidthInBytes = width * 4;
	copy.Height = height;
	CU_CHECK(cuMemcpy2D(&copy));

	NV_ENC_MAP_INPUT_RESOURCE param4{};
	param4.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
	param4.registeredResource = nvenc_resource;
	NVENC_CHECK(fn.nvEncMapInputResource(session_handle, &param4));

	NV_ENC_PIC_PARAMS param{};
	param.version = NV_ENC_PIC_PARAMS_VER;
	param.inputWidth = width;
	param.inputHeight = height;
	param.inputPitch = width;
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
