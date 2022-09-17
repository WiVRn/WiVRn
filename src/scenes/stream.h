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

#pragma once

#include "decoder/shard_accumulator.h"
#include "scene.h"
#include "stream_reprojection.h"
#include "utils/sync_queue.h"
#include "vk/device_memory.h"
#include "vk/renderpass.h"
#include "wivrn_client.h"
#include "wivrn_packets.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vulkan/vulkan_core.h>

namespace scenes
{
class stream : public scene, public std::enable_shared_from_this<stream>
{
	static const size_t view_count = 2;

	using stream_description = xrt::drivers::wivrn::to_headset::video_stream_description::item;

	struct accumulator_images
	{
		std::unique_ptr<shard_accumulator> decoder;
		// latest frames from oldest to most recent
		std::array<std::shared_ptr<shard_accumulator::blit_handle>, 2> latest_frames;

		static std::optional<uint64_t> common_frame(const std::vector<accumulator_images> &);
		std::shared_ptr<shard_accumulator::blit_handle> frame(std::optional<uint64_t> id);
		std::vector<uint64_t> frames() const;
	};

	struct renderpass_output
	{
		VkExtent2D size;
		VkFormat format;
		VkImage image;
		VkDeviceMemory memory;
		VkImageView image_view;
	};

	std::unique_ptr<wivrn_session> network_session;
	std::atomic<bool> exiting = false;
	std::thread network_thread;
	std::thread tracking_thread;
	std::thread video_thread;

	utils::sync_queue<std::variant<to_headset::video_stream_data_shard, to_headset::video_stream_parity_shard>> shard_queue;

	std::mutex decoder_mutex;
	std::vector<accumulator_images> decoders; // Locked by decoder_mutex

	std::array<renderpass_output, view_count> decoder_output{};

	stream_reprojection reprojector;

	VkFence fence;
	VkCommandBuffer command_buffer;

	std::array<std::pair<XrAction, XrPath>, 2> haptics_actions;
	std::vector<std::tuple<device_id, XrAction, XrActionType>> input_actions;

	bool ready_ = false;
	bool video_started_ = false;
	XrTime first_frame_time{};
	const float dbrightness = 2;

	stream() = default;

public:
	~stream();

	static std::shared_ptr<stream> create(std::unique_ptr<wivrn_session> session);

	virtual void render() override;

	void operator()(to_headset::video_stream_data_shard &&);
	void operator()(to_headset::video_stream_parity_shard &&);
	void operator()(to_headset::haptics &&);
	void operator()(to_headset::timesync_query &&);
	void operator()(to_headset::video_stream_description &&);

	VkFormat swapchain_format()
	{
		return swapchains[0].format();
	}

	void push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle);

	void send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback);

	bool ready() const
	{
		return ready_;
	}

	bool alive() const
	{
		return !exiting;
	}

private:
	void process_packets();
	void tracking();
	void video();
	void read_actions();

	void setup(const to_headset::video_stream_description &);
	void cleanup();
};
} // namespace scenes
