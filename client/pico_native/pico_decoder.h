#pragma once

#include "wivrn_packets.h"

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>

struct pico_decoded_frame
{
	AHardwareBuffer * hardware_buffer = nullptr;
	int width = 0;
	int height = 0;
	uint64_t frame_index = 0;
	bool valid = false;

	~pico_decoded_frame()
	{
		if (hardware_buffer)
			AHardwareBuffer_release(hardware_buffer);
	}
};

class pico_video_decoder
{
public:
	using frame_callback = std::function<void(std::shared_ptr<pico_decoded_frame>)>;

private:
	uint8_t stream_index;

	std::shared_ptr<AImageReader> image_reader;

	AMediaCodec * media_codec = nullptr;

	struct pending_frame
	{
		std::vector<uint8_t> data;
		uint64_t frame_index = 0;
	};
	std::mutex pending_mutex;
	std::vector<pending_frame> pending_frames;
	std::condition_variable pending_cv;

	struct frame_info
	{
		uint64_t frame_index;
		wivrn::from_headset::feedback feedback;
		wivrn::to_headset::video_stream_data_shard::view_info_t view_info;
	};
	std::mutex frame_info_mutex;
	std::vector<frame_info> pending_frame_infos;

	std::atomic<bool> exiting = false;
	std::thread worker;

	frame_callback on_frame_decoded;

	void on_image_available(AImageReader * reader);
	static void on_image_available_cb(void * ctx, AImageReader * reader);

	void worker_loop();

public:
	pico_video_decoder(
		const wivrn::to_headset::video_stream_description & desc,
		uint8_t stream_idx,
		frame_callback callback);

	~pico_video_decoder();

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial);

	void frame_completed(
		const wivrn::from_headset::feedback & feedback,
		const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info);

	static void supported_codecs(std::vector<wivrn::video_codec> & result);
};
