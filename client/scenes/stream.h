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

#include "utils/named_thread.h"
#include "decoder/shard_accumulator.h"
#include "scene.h"
#include "stream_reprojection.h"
#include "utils/sync_queue.h"
#include "wivrn_client.h"
#include "wivrn_packets.h"
#include <mutex>
#include <thread>
#include <vulkan/vulkan_core.h>
#include "audio/audio.h"

namespace scenes
{
class stream : public scene_impl<stream>, public std::enable_shared_from_this<stream>
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
		vk::Extent2D size;
		vk::Format format;
		image_allocation image;
		vk::raii::ImageView image_view = nullptr;
	};

	std::unique_ptr<wivrn_session> network_session;
	std::atomic<bool> exiting = false;
	std::thread network_thread;
	std::optional<std::thread> tracking_thread;
	std::thread video_thread;

	utils::sync_queue<to_headset::video_stream_data_shard> shard_queue;

	std::mutex decoder_mutex;
	std::vector<accumulator_images> decoders; // Locked by decoder_mutex

	std::array<renderpass_output, view_count> decoder_output{};

	std::optional<stream_reprojection> reprojector;

	vk::raii::Fence fence = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	std::array<std::pair<XrAction, XrPath>, 2> haptics_actions;
	std::vector<std::tuple<device_id, XrAction, XrActionType>> input_actions;

	bool ready_ = false;
	bool video_started_ = false;
	XrTime first_frame_time{};
	const float dbrightness = 2;

	std::vector<xr::swapchain> swapchains;
	vk::Format swapchain_format;

	std::optional<audio> audio_handle;

	stream() = default;

public:
	~stream();

	static std::shared_ptr<stream> create(std::unique_ptr<wivrn_session> session);

	void render() override;

	void operator()(to_headset::handshake&&) {};
	void operator()(to_headset::video_stream_data_shard &&);
	void operator()(to_headset::haptics &&);
	void operator()(to_headset::timesync_query &&);
	void operator()(to_headset::audio_stream_description &&);
	void operator()(to_headset::video_stream_description &&);
	void operator()(audio_data&&);

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

	static meta& get_meta_scene();

private:
	void process_packets();
	void tracking();
	void video();
	void read_actions();

	void setup(const to_headset::video_stream_description &);
	void exit();
	void cleanup();
};
} // namespace scenes
