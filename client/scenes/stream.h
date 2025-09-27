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

#include "app_launcher.h"
#include "audio/audio.h"
#include "decoder/shard_accumulator.h"
#include "render/imgui_impl.h"
#include "scene.h"
#include "scenes/blitter.h"
#include "scenes/input_profile.h"
#include "stream_defoveator.h"
#include "utils/thread_safe.h"
#include "wifi_lock.h"
#include "wivrn_client.h"
#include "wivrn_packets.h"
#include "xr/space.h"
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vulkan/vulkan_core.h>

namespace scenes
{
class stream : public scene_impl<stream>, public std::enable_shared_from_this<stream>
{
public:
	enum class state
	{
		initializing,
		streaming,
		stalled
	};
	static const size_t image_buffer_size = 3;

	app_launcher apps;

private:
	static const size_t view_count = 2;

	using stream_description = wivrn::to_headset::video_stream_description::item;

	struct accumulator_images
	{
		std::unique_ptr<wivrn::shard_accumulator> decoder;
		// latest frames, rolling buffer
		std::array<std::shared_ptr<wivrn::shard_accumulator::blit_handle>, image_buffer_size> latest_frames;

		std::shared_ptr<wivrn::shard_accumulator::blit_handle> frame(uint64_t id) const;
		bool alpha() const;
		bool empty() const;
	};

	wifi_lock::wifi wifi;

	// for frames inside accumulator images
	std::mutex frames_mutex;
	std::vector<std::shared_ptr<wivrn::shard_accumulator::blit_handle>> common_frame(XrTime display_time);

	std::unique_ptr<wivrn_session> network_session;
	std::atomic<bool> exiting = false;
	std::thread network_thread;
	thread_safe<to_headset::tracking_control> tracking_control{};
	std::array<std::atomic<interaction_profile>, 2> interaction_profiles; // left and right hand
	std::atomic<bool> interaction_profile_changed = false;
	std::atomic<bool> recenter_requested = false;
	std::atomic<XrDuration> display_time_phase = 0;
	std::atomic<XrDuration> display_time_period = 0;
	XrTime last_display_time = 0;
	std::atomic<XrDuration> real_display_period = 0;
	std::optional<std::thread> tracking_thread;

	std::shared_mutex decoder_mutex;
	std::optional<to_headset::video_stream_description> video_stream_description;
	std::vector<accumulator_images> decoders; // Locked by decoder_mutex

	std::array<blitter, view_count> blitters;

	std::optional<stream_defoveator> defoveator;

	vk::raii::Fence fence = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	struct haptics_action
	{
		XrAction action;
		XrPath path;
		float amplitude;
	};
	std::unordered_multimap<device_id, haptics_action> haptics_actions;
	std::vector<std::tuple<device_id, XrAction, XrActionType>> input_actions;

	state state_ = state::initializing;

	xr::swapchain swapchain;

	std::optional<audio> audio_handle;

	std::optional<xr::hand_tracker> left_hand;
	std::optional<xr::hand_tracker> right_hand;
	std::optional<input_profile> input;
	static inline const uint32_t layer_controllers = 1 << 0;
	static inline const uint32_t layer_rays = 1 << 1;

	// Size of the composition layer used for the controllers
	uint32_t width;
	uint32_t height;

	std::optional<imgui_context> imgui_ctx;
	enum class gui_status
	{
		hidden,
		overlay_only,
		compact,
		stats,
		settings,
		foveation_settings,
		applications,
		application_launcher,
	};

	bool is_gui_interactable() const;

	std::atomic<gui_status> gui_status = gui_status::hidden;
	enum gui_status last_gui_status = gui_status::hidden;
	enum gui_status next_gui_status = gui_status::stats;
	XrTime gui_status_last_change;
	float dimming = 0;

	XrAction plots_toggle_1 = XR_NULL_HANDLE;
	XrAction plots_toggle_2 = XR_NULL_HANDLE;
	XrAction recenter_left = XR_NULL_HANDLE;
	XrAction recenter_right = XR_NULL_HANDLE;
	XrAction foveation_pitch = XR_NULL_HANDLE;
	XrAction foveation_distance = XR_NULL_HANDLE;
	XrAction foveation_ok = XR_NULL_HANDLE;
	XrAction foveation_cancel = XR_NULL_HANDLE;

	// Position of the GUI relative to the view space, in view space axes
	glm::vec3 head_gui_position{-0.1, -0.3, -1.2}; // Shift 10cm left by default so that the stats are centered accounting for the tab list
	glm::quat head_gui_orientation{1, 0, 0, 0};

	bool override_foveation_enable;
	float override_foveation_pitch; // The pitch is the opposite as the height displayed in the GUI
	float override_foveation_distance;

	// Which controller is used for recentering and position of the GUI relative to the controller, in controller axes, during recentering
	std::optional<std::tuple<xr::spaces, glm::vec3, glm::quat>> recentering_context;
	void update_gui_position(xr::spaces controller);

	// Keep a reference to the resources needed to blit the images until vkWaitForFences
	std::vector<std::shared_ptr<wivrn::shard_accumulator::blit_handle>> current_blit_handles;

	XrTime running_application_req = 0;
	thread_safe<to_headset::running_applications> running_applications;

	stream(std::string server_name, scene & parent_scene);

public:
	~stream();

	static std::shared_ptr<stream> create(
	        std::unique_ptr<wivrn_session> session,
	        float guessed_fps,
	        std::string server_name,
	        scene & parent_scene);

	void render(const XrFrameState &) override;
	void on_focused() override;
	void on_unfocused() override;
	void on_xr_event(const xr::event &) override;

	void operator()(to_headset::crypto_handshake &&) {};
	void operator()(to_headset::pin_check_2 &&) {};
	void operator()(to_headset::pin_check_4 &&) {};
	void operator()(to_headset::handshake &&) {};
	void operator()(to_headset::video_stream_data_shard &&);
	void operator()(to_headset::haptics &&);
	void operator()(to_headset::timesync_query &&);
	void operator()(to_headset::tracking_control &&);
	void operator()(to_headset::audio_stream_description &&);
	void operator()(to_headset::video_stream_description &&);
	void operator()(to_headset::refresh_rate_change &&);
	void operator()(to_headset::application_list &&);
	void operator()(to_headset::application_icon &&);
	void operator()(to_headset::running_applications &&);
	void operator()(audio_data &&);

	void push_blit_handle(wivrn::shard_accumulator * decoder, std::shared_ptr<wivrn::shard_accumulator::blit_handle> handle);

	void send_feedback(const wivrn::from_headset::feedback & feedback);

	state current_state() const
	{
		return state_;
	}

	bool alive() const
	{
		return !exiting;
	}

	void start_application(std::string appid);

	static meta & get_meta_scene();

private:
	void process_packets();
	void tracking();
	void read_actions();

	void on_interaction_profile_changed(const XrEventDataInteractionProfileChanged &);

	void setup(const to_headset::video_stream_description &);
	void setup_reprojection_swapchain(uint32_t width, uint32_t height);
	void exit();

	vk::raii::QueryPool query_pool = nullptr;
	bool query_pool_filled = false;

	// Used for plots
	uint64_t bytes_received = 0;
	uint64_t bytes_sent = 0;
	float bandwidth_rx = 0;
	float bandwidth_tx = 0;

	struct gpu_timestamps
	{
		float gpu_barrier = 0;
		float gpu_time = 0;
	};

	struct global_metric //: gpu_timestamps
	{
		float gpu_barrier;
		float gpu_time;
		float cpu_time = 0;
		float bandwidth_rx = 0;
		float bandwidth_tx = 0;
	};

	struct plot
	{
		std::string title;
		struct subplot
		{
			std::string title;
			float scenes::stream::global_metric::*data;
		};
		std::vector<subplot> subplots;
		const char * unit;
	};

	static const inline int size_gpu_timestamps = 1 + sizeof(gpu_timestamps) / sizeof(float);

	struct decoder_metric
	{
		// All times are in seconds relative to encode_begin
		float encode_begin;
		float encode_end;
		float send_begin;
		float send_end;
		float received_first_packet;
		float received_last_packet;
		float sent_to_decoder;
		float received_from_decoder;
		float blitted;
		float displayed;
		float predicted_display;
	};

	std::vector<global_metric> global_metrics{300};
	std::vector<std::vector<decoder_metric>> decoder_metrics;
	std::vector<float> axis_scale;
	XrTime last_metric_time = 0;
	int metrics_offset = 0;

	// Used for compact view
	float compact_bandwidth_rx = 0;
	float compact_bandwidth_tx = 0;
	float compact_cpu_time = 0;
	float compact_gpu_time = 0;

	void accumulate_metrics(XrTime predicted_display_time, const std::vector<std::shared_ptr<wivrn::shard_accumulator::blit_handle>> & blit_handles, const gpu_timestamps & timestamps);
	void gui_performance_metrics();
	void gui_compact_view();
	void gui_settings();
	void gui_foveation_settings(float predicted_display_period);
	void gui_applications();
	void draw_gui(XrTime predicted_display_time, XrDuration predicted_display_period);
};
} // namespace scenes
