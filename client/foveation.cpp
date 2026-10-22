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

#include "foveation.h"

#include "application.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/system.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace
{
float convergence_angle(float distance, float eye_x, float yaw)
{
	const float target_x = distance * std::sin(yaw);
	const float target_z = distance * std::cos(yaw);

	return std::atan2(target_x - eye_x, target_z);
}

XrVector2f yaw_pitch(const XrQuaternionf & q)
{
	const float sine_theta = std::clamp(-2.0f * (q.y * q.z - q.w * q.x), -1.0f, 1.0f);
	const float pitch = std::asin(sine_theta);

	if (std::abs(sine_theta) > 0.99999f)
	{
		const float scale = std::copysign(2.0f, sine_theta);
		return {scale * std::atan2(-q.z, q.w), pitch};
	}

	return {
	        std::atan2(2.0f * (q.x * q.z + q.w * q.y),
	                   q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z),
	        pitch};
}

float ndc_to_angle(float center, float a0, float a1)
{
	const float u = std::clamp(center * 0.5f + 0.5f, 0.0f, 1.0f);
	const float tangent = std::lerp(std::tan(a0), std::tan(a1), u);
	return std::atan(tangent);
}

client_foveation::angles center_to_angles(const std::array<XrVector2f, 2> & center, std::span<const XrFovf> fovs)
{
	assert(fovs.size() == 2);
	client_foveation::angles result{};

	for (size_t i = 0; i < result.size(); ++i)
	{
		result[i].x = ndc_to_angle(center[i].x, fovs[i].angleLeft, fovs[i].angleRight);
		result[i].y = ndc_to_angle(center[i].y, fovs[i].angleUp, fovs[i].angleDown);
	}

	return result;
}

bool fov_equal(const XrFovf & a, const XrFovf & b, const float epsilon)
{
	return std::abs(a.angleLeft - b.angleLeft) <= epsilon and
	       std::abs(a.angleRight - b.angleRight) <= epsilon and
	       std::abs(a.angleUp - b.angleUp) <= epsilon and
	       std::abs(a.angleDown - b.angleDown) <= epsilon;
}

client_foveation::angles target_angles(
        float yaw,
        float pitch,
        float convergence_distance,
        std::span<const XrView> views)
{
	assert(views.size() == 2);
	client_foveation::angles result{};

	for (size_t i = 0; i < result.size(); ++i)
	{
		result[i].x = convergence_angle(convergence_distance, views[i].pose.position.x, yaw);
		result[i].y = pitch;
	}

	return result;
}

std::optional<client_foveation::angles> eye_gaze_angles(
        XrSpace eye_gaze_space,
        XrSpace view_space,
        XrTime time,
        XrViewStateFlags view_flags,
        std::span<const XrView> views)
{
	if (not eye_gaze_space or not view_space or views.size() != 2)
		return {};

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	};

	XrResult res = xrLocateSpace(eye_gaze_space, view_space, time, &location);
	if (XR_FAILED(res) or
	    not(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) or
	    not(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT))
		return {};

	if (not(view_flags & XR_VIEW_STATE_POSITION_VALID_BIT))
		return {};

	const XrVector2f gaze = yaw_pitch(location.pose.orientation);
	return target_angles(-gaze.x, gaze.y, client_foveation::default_convergence_distance, views);
}

} // namespace

namespace client_foveation
{

center::center(xr::instance & inst, xr::system & sys, xr::session & xr_session, vk::raii::Device & vk_device, vk::raii::CommandPool & vk_commandpool, thread_safe<vk::raii::Queue> & vk_queue, vk::Format format) :
        instance(inst),
        session(xr_session),
        device(vk_device),
        commandpool(vk_commandpool),
        queue(vk_queue),
        swapchain_format(format)
{
	if (application::get_foveation_center_supported() and
	    application::get_foveation_vulkan_supported() and
	    inst.has_extension(XR_META_FOVEATION_EYE_TRACKED_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_VULKAN_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME) and
	    inst.has_extension(XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME) and
	    sys.foveation_eye_tracked_properties().supportsFoveationEyeTracked)
	{
		xrCreateFoveationProfileFB = inst.get_proc<PFN_xrCreateFoveationProfileFB>("xrCreateFoveationProfileFB");
		xrDestroyFoveationProfileFB = inst.get_proc<PFN_xrDestroyFoveationProfileFB>("xrDestroyFoveationProfileFB");
		xrUpdateSwapchainFB = inst.get_proc<PFN_xrUpdateSwapchainFB>("xrUpdateSwapchainFB");
		xrGetFoveationEyeTrackedStateMETA = inst.get_proc<PFN_xrGetFoveationEyeTrackedStateMETA>("xrGetFoveationEyeTrackedStateMETA");

		if (xrCreateFoveationProfileFB and xrDestroyFoveationProfileFB and
		    xrUpdateSwapchainFB and xrGetFoveationEyeTrackedStateMETA)
		{
			XrFoveationEyeTrackedProfileCreateInfoMETA eye_tracked_info{
			        .type = XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META,
			        .flags = 0,
			};
			XrFoveationLevelProfileCreateInfoFB level_info{
			        .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
			        .next = &eye_tracked_info,
			        .level = XR_FOVEATION_LEVEL_LOW_FB,
			        .verticalOffset = 0,
			        .dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB,
			};
			XrFoveationProfileCreateInfoFB profile_info{
			        .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
			        .next = &level_info,
			};

			XrFoveationProfileFB raw_profile = XR_NULL_HANDLE;
			XrResult profile_result = xrCreateFoveationProfileFB(session, &profile_info, &raw_profile);
			if (XR_SUCCEEDED(profile_result))
				foveation_profile.emplace(raw_profile, xrDestroyFoveationProfileFB);
		}
	}

	if (foveation_profile.has_value())
		spdlog::info("XR_META_foveation_eye_tracked will be used for this stream");
	else if (application::get_eye_gaze_supported())
		spdlog::info("Eye gaze tracking will be used for this stream");
	else
		spdlog::info("Static foveation will be used for this stream");

	auto [flags, views] = session.locate_views(
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	        instance.now(),
	        application::space(xr::spaces::view));
	(void)flags;
	assert(views.size() == headset_fov.size());
	for (size_t i = 0; i < headset_fov.size(); ++i)
		headset_fov[i] = views[i].fov;

	const auto & config = application::get_config();
	set_manual_mode({
	        .enabled = config.override_foveation_enable,
	        .pitch = config.override_foveation_pitch,
	        .distance = config.override_foveation_distance,
	});
}

// member order ensures swapchain is dropped before foveation profile
center::~center() = default;

bool center::supports_meta() const
{
	return foveation_profile.has_value();
}

manual_override center::get_manual_mode() const
{
	return *manual_mode_.lock();
}

void center::set_manual_mode(manual_override mode)
{
	*manual_mode_.lock() = mode;
}

bool center::is_cropped(std::span<const XrFovf> submitted_fov, std::span<const bool> valid) const
{
	assert(submitted_fov.size() == view_count and valid.size() == view_count);
	for (size_t i = 0; i < view_count; ++i)
	{
		// generous epsilon of ~6° per-side to filter out false positives
		if (valid[i] and not fov_equal(submitted_fov[i], headset_fov[i], 0.1f))
			return true;
	}
	return false;
}

bool center::needs_swapchain_recreation(bool cropped) const
{
	return supports_meta() and (not cropped_mode or *cropped_mode != cropped);
}

bool center::use_meta_swapchain(bool cropped) const
{
	return supports_meta() and not cropped;
}

void center::set_cropped(bool cropped)
{
	if (not cropped_mode or *cropped_mode != cropped)
		spdlog::info("Stream FOV mode: {}", cropped ? "cropped" : "full");
	cropped_mode = cropped;
}

void center::on_swapchain_recreated(bool cropped)
{
	dummy_swapchain = {};
	if (supports_meta() and cropped)
		create_dummy_swapchain();
	set_cropped(cropped);
}

bool center::update_meta_swapchain(XrSwapchain swapchain) const
{
	if (not supports_meta() or not swapchain)
		return false;

	XrSwapchainStateFoveationFB swapchain_state{
	        .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
	        .flags = 0,
	        .profile = *foveation_profile,
	};

	XrResult update_result = xrUpdateSwapchainFB(
	        swapchain,
	        reinterpret_cast<const XrSwapchainStateBaseHeaderFB *>(&swapchain_state));
	return XR_SUCCEEDED(update_result);
}

std::optional<angles> center::meta_angles(XrSwapchain swapchain, std::span<const XrFovf> fov) const
{
	if (not update_meta_swapchain(swapchain))
		return {};

	XrFoveationEyeTrackedStateMETA state{
	        .type = XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META,
	};

	XrResult result = xrGetFoveationEyeTrackedStateMETA(session, &state);
	if (XR_FAILED(result) or not(state.flags & XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META))
		return {};

	std::array<XrVector2f, view_count> ndc_center{state.foveationCenter[0], state.foveationCenter[1]};
	return center_to_angles(ndc_center, fov);
}

void center::update(std::span<const XrFovf> submitted_fov, std::span<const bool> valid, XrSwapchain stream_swapchain, XrTime display_time)
{
	assert(submitted_fov.size() == view_count and valid.size() == view_count);
	layer_.reset();
	packet_.reset();

	XrSpace view_space = application::space(xr::spaces::view);
	auto [view_flags, views] = session.locate_views(
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, display_time, view_space);

	constexpr XrViewStateFlags layer_flags =
	        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
	if (supports_meta() and cropped_mode.value_or(false) and dummy_swapchain and
	    views.size() == view_count and (view_flags & layer_flags) == layer_flags)
	{
		projection_layer layer{
		        .flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		        .space = view_space,
		};
		for (size_t i = 0; i < view_count; ++i)
		{
			layer.views[i] = {
			        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			        .pose = views[i].pose,
			        .fov = headset_fov[i],
			        .subImage = {
			                .swapchain = dummy_swapchain,
			                .imageRect = {
			                        .offset = {0, 0},
			                        .extent = {dummy_swapchain_size, dummy_swapchain_size},
			                },
			                .imageArrayIndex = uint32_t(i),
			        },
			};
		}
		layer_ = layer;
	}

	if (views.size() != view_count or not(view_flags & XR_VIEW_STATE_POSITION_VALID_BIT))
		return;

	// dummy layer is always full FOV
	std::array<XrFovf, view_count> effective_fov{};
	for (size_t i = 0; i < view_count; ++i)
		effective_fov[i] = valid[i] ? submitted_fov[i] : headset_fov[i];
	const XrSwapchain meta_swapchain = cropped_mode.value_or(false) ? static_cast<XrSwapchain>(dummy_swapchain) : stream_swapchain;
	const std::span<const XrFovf> meta_fov = cropped_mode.value_or(false) ? std::span<const XrFovf>(headset_fov) : std::span<const XrFovf>(effective_fov);

	const auto manual = get_manual_mode();
	std::optional<angles> angles;
	if (manual.enabled)
	{
		if (supports_meta())
			update_meta_swapchain(meta_swapchain);
		angles = target_angles(0, manual.pitch, manual.distance, views);
	}
	else
	{
		if (supports_meta())
		{
			if (auto a = meta_angles(meta_swapchain, meta_fov))
				angles = *a;
		}
		if (not angles and application::get_eye_gaze_supported())
		{
			if (auto a = eye_gaze_angles(application::space(xr::spaces::eye_gaze),
			                             view_space,
			                             display_time,
			                             view_flags,
			                             views))
				angles = *a;
		}
		if (not angles)
			angles = target_angles(0, default_foveation_pitch, default_convergence_distance, views);
	}
	// angles guaranteed to have a value at this point
	packet_ = wivrn::from_headset::foveation{.angles = *angles};
}

std::optional<center::projection_layer> center::get_layer() const
{
	return layer_;
}

std::optional<wivrn::from_headset::foveation> center::take_packet()
{
	// A skipped render must not resend the previous frame's center.
	auto result = packet_;
	packet_.reset();
	return result;
}

void center::create_dummy_swapchain()
{
	device.waitIdle();

	dummy_swapchain = xr::swapchain(
	        instance,
	        session,
	        device,
	        swapchain_format,
	        dummy_swapchain_size,
	        dummy_swapchain_size,
	        1,
	        view_count,
	        true,
	        true);

	if (not update_meta_swapchain(dummy_swapchain))
		throw std::runtime_error("Failed to apply eye-tracked foveation profile to dummy swapchain");

	const int image_index = dummy_swapchain.acquire();
	dummy_swapchain.wait();

	auto clear_command_buffer = std::move(device.allocateCommandBuffers({
	        .commandPool = *commandpool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);
	auto clear_fence = device.createFence({});

	clear_command_buffer.begin(vk::CommandBufferBeginInfo{
	        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	});

	vk::ImageMemoryBarrier to_transfer{
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eTransferDstOptimal,
	        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .image = dummy_swapchain.image(image_index),
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = 0,
	                .layerCount = view_count,
	        },
	};
	clear_command_buffer.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTopOfPipe,
	        vk::PipelineStageFlagBits::eTransfer,
	        {},
	        {},
	        {},
	        to_transfer);

	clear_command_buffer.clearColorImage(
	        dummy_swapchain.image(image_index),
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ClearColorValue{std::array<float, 4>{0, 0, 0, 0}},
	        to_transfer.subresourceRange);

	vk::ImageMemoryBarrier to_runtime{
	        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
	        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
	        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
	        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .image = dummy_swapchain.image(image_index),
	        .subresourceRange = to_transfer.subresourceRange,
	};
	clear_command_buffer.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::PipelineStageFlagBits::eAllCommands,
	        {},
	        {},
	        {},
	        to_runtime);
	clear_command_buffer.end();

	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*clear_command_buffer);
	queue.lock()->submit(submit_info, *clear_fence);
	if (device.waitForFences(*clear_fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout while clearing foveation dummy swapchain");

	dummy_swapchain.release();
	spdlog::info(
	        "Created {}x{} eye-tracked foveation dummy swapchain",
	        dummy_swapchain_size,
	        dummy_swapchain_size);
}

} // namespace client_foveation
