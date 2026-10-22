/*
 * WiVRn VR streaming
 * Copyright (C) 2026 galister <galister-dev@pm.me>
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

#include "utils/handle.h"
#include "utils/thread_safe.h"
#include "wivrn_packets.h"
#include "xr/swapchain.h"

#include <array>
#include <optional>
#include <span>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;
class system;
} // namespace xr

namespace client_foveation
{
using angles = std::array<XrVector2f, 2>;

inline constexpr float default_convergence_distance = 1.0f;
// Default foveation target is 1 m forward and 10 degrees below horizontal.
inline constexpr float default_foveation_pitch = -0.17453292519943295f; // -10 degrees

struct manual_override
{
	bool enabled = false;
	float pitch = default_foveation_pitch;
	float distance = default_convergence_distance;
};

// Unified class for static, meta and eye-gaze based foveation.
class center
{
public:
	static constexpr size_t view_count = 2;
	static constexpr int32_t dummy_swapchain_size = 512;

	struct projection_layer
	{
		XrCompositionLayerFlags flags;
		XrSpace space;
		std::array<XrCompositionLayerProjectionView, view_count> views;
	};

	center(xr::instance &, xr::system &, xr::session &, vk::raii::Device &, vk::raii::CommandPool &, thread_safe<vk::raii::Queue> &, vk::Format swapchain_format);
	~center();

	bool supports_meta() const;

	manual_override get_manual_mode() const;
	void set_manual_mode(manual_override mode);

	bool is_cropped(std::span<const XrFovf> submitted_fov, std::span<const bool> valid) const;
	bool needs_swapchain_recreation(bool cropped) const;
	bool use_meta_swapchain(bool cropped) const;
	void on_swapchain_recreated(bool cropped);
	void set_cropped(bool cropped);

	void update(std::span<const XrFovf> submitted_fov, std::span<const bool> valid, XrSwapchain stream_swapchain, XrTime display_time);
	std::optional<projection_layer> get_layer() const;
	std::optional<wivrn::from_headset::foveation> take_packet();

private:
	mutable thread_safe<manual_override> manual_mode_{};

	xr::instance & instance;
	xr::session & session;
	vk::raii::Device & device;
	vk::raii::CommandPool & commandpool;
	thread_safe<vk::raii::Queue> & queue;
	vk::Format swapchain_format;

	PFN_xrCreateFoveationProfileFB xrCreateFoveationProfileFB = nullptr;
	PFN_xrDestroyFoveationProfileFB xrDestroyFoveationProfileFB = nullptr;
	PFN_xrUpdateSwapchainFB xrUpdateSwapchainFB = nullptr;
	PFN_xrGetFoveationEyeTrackedStateMETA xrGetFoveationEyeTrackedStateMETA = nullptr;
	std::optional<utils::handle<XrFoveationProfileFB>> foveation_profile;

	std::array<XrFovf, view_count> headset_fov{};
	std::optional<bool> cropped_mode;
	xr::swapchain dummy_swapchain;
	std::optional<projection_layer> layer_;
	std::optional<wivrn::from_headset::foveation> packet_;

	bool update_meta_swapchain(XrSwapchain swapchain) const;
	std::optional<angles> meta_angles(XrSwapchain swapchain, std::span<const XrFovf> fov) const;
	void create_dummy_swapchain();
};

} // namespace client_foveation
