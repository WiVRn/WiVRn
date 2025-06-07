/*
 * WiVRn VR streaming
 * Copyright (C) 2024  galister <galister@librevr.org>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "render/render_interface.h"
#include "vk/vk_mini_helpers.h"

#include "wivrn_foveation.h"

#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "utils/wivrn_vk_bundle.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"

#include <array>
#include <cmath>
#include <map>
#include <ranges>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <openxr/openxr.h>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

bool render_distortion_images_ensure(struct render_resources * r,
                                     struct vk_bundle * vk,
                                     struct xrt_device * xdev,
                                     bool pre_rotate)
{
	if (r->distortion.buffer == VK_NULL_HANDLE)
	{
		return vk_buffer_init(vk,
		                      sizeof(render_compute_distortion_foveation_data),
		                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		                      &r->distortion.buffer,
		                      &r->distortion.device_memory);
	}
	return true;
}

void render_distortion_images_fini(struct render_resources * r)
{
	auto * vk = r->vk;
	D(Buffer, r->distortion.buffer);
	DF(Memory, r->distortion.device_memory);
}

static double foveate(double a, double b, double λ, double c, double x)
{
	// In order to save encoding, transmit and decoding time, only a portion of the image is encoded in full resolution.
	// on each axis, foveated coordinates are defined by the following formula.
	return λ / a * tan(a * x + b) + c;
	// a and b are defined such as:
	// edges of the image are not moved
	// f(-1) = -1
	// f( 1) =  1
	// the function also enforces pixel ratio 1:1 at fovea
	// df⁻¹(x)/dx = 1/scale for x = c

	// We then ensure that source and destination pixel grids match by
	// rounding to integer pixel ratios: 1:1, 1:2 etc.
	// Finally, pixel spans are sorted so that we only have increasing
	// ratios going out from the center.
}

static std::tuple<float, float> solve_foveation(float λ, float c)
{
	// Compute a and b for the foveation function such that:
	//   foveate(a, b, scale, c, -1) = -1   (eq. 1)
	//   foveate(a, b, scale, c,  1) =  1   (eq. 2)
	//
	// Use eq. 2 to express a as function of b, then replace in eq. 1
	// equation that needs to be null is:
	auto b = [λ, c](double a) { return atan(a * (1 - c) / λ) - a; };
	auto eq = [λ, c](double a) { return atan(a * (1 - c) / λ) + atan(a * (1 + c) / λ) - 2 * a; }; // (eq. 3)

	// function starts positive, reaches a maximum then decreases to -∞
	double a0 = 0;
	// Find a negative value by computing eq(2^n)
	double a1 = 1;
	while (eq(a1) > 0)
		a1 *= 2;

	// last computed values for f(a0) and f(a1)
	std::optional<double> f_a0;
	double f_a1 = eq(a1);

	int n = 0;
	double a = 0;
	while (std::abs(a1 - a0) > 0.0000001 && n++ < 100)
	{
		if (not f_a0)
		{
			// use binary search
			a = 0.5 * (a0 + a1);
			double val = eq(a);
			if (val > 0)
			{
				a0 = a;
				f_a0 = val;
			}
			else
			{
				a1 = a;
				f_a1 = val;
			}
		}
		else
		{
			// f(a1) is always defined
			// when f(a0) is defined, use secant method
			a = a1 - f_a1 * (a1 - a0) / (f_a1 - *f_a0);
			a0 = a1;
			a1 = a;
			f_a0 = f_a1;
			f_a1 = eq(a);
		}
	}

	return {a, b(a)};
}

static xrt_vec2 yaw_pitch(xrt_quat q)
{
	float sine_theta = std::clamp(-2.0f * (q.y * q.z - q.w * q.x), -1.0f, 1.0f);

	float pitch = std::asin(sine_theta);

	if (std::abs(sine_theta) > 0.99999f)
	{
		float scale = std::copysign(2.0, sine_theta);
		return {scale * std::atan2(-q.z, q.w), pitch};
	}

	return {
	        std::atan2(2.0f * (q.x * q.z + q.w * q.y),
	                   q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z),
	        pitch};
}

static float angles_to_center(float e, float l, float r)
{
	return (e - l) / (r - l) * 2 - 1;
}

static float convergence_angle(float eye_x, float gaze_yaw)
{
	const float c = 0.5; // simutaled convergence distance
	auto b = c * std::sin(gaze_yaw) - eye_x;
	return std::asin(b / c);
}

void fill_param_2d(
        float c,
        size_t foveated_dim,
        size_t source_dim,
        std::vector<uint16_t> & out)
{
	float scale = float(foveated_dim) / source_dim;
	auto [a, b] = solve_foveation(scale, c);

	uint16_t last = 0;
	std::vector<uint16_t> left; // index 0: 1:1 ratio, then 2:1 etc.
	std::vector<uint16_t> right;
	for (size_t i = 1; i < foveated_dim; ++i)
	{
		double u = (i * 2.) / foveated_dim - 1;
		auto f = foveate(a, b, scale, c, u);
		uint16_t n = std::clamp<uint16_t>((f * 0.5 + 0.5) * source_dim + 0.5, 0, source_dim);
		assert(n > last);
		size_t count = n - last;
		auto & vec = u < c ? left : right;
		if (count > vec.size())
			vec.resize(count);
		vec[count - 1]++;
		last = n;
	}
	assert(last < source_dim);
	size_t count = source_dim - last;
	if (count > right.size())
		right.resize(count);
	right[count - 1]++;

	count = std::max(left.size(), right.size());
	out.clear();
	out.resize(count - left.size());
	out.insert(out.end(), left.rbegin(), left.rend());
	if (not right.empty())
		out.back() += right.front();
	if (right.size() > 1)
		out.insert(out.end(), right.begin() + 1, right.end());
	out.resize(count * 2 - 1);
}

namespace wivrn
{

void wivrn_foveation::compute_params(
        xrt_rect src[2],
        const xrt_fov fovs[2])
{
	std::lock_guard lock(mutex);

	xrt_vec2 tan_center[2]{};
	if (gaze.x != 0 or gaze.y != 0 or gaze.z != 0 or gaze.w != 0)
	{
		auto e = yaw_pitch(gaze);
		for (size_t i = 0; i < 2; ++i)
		{
			xrt_quat view_quat{
			        .x = views[i].pose.orientation.x,
			        .y = views[i].pose.orientation.y,
			        .z = views[i].pose.orientation.z,
			        .w = views[i].pose.orientation.w};
			auto view = yaw_pitch(view_quat);

			auto angle_x = convergence_angle(views[i].pose.position.x, e.x);
			tan_center[i].x = angles_to_center(view.x + angle_x, views[i].fov.angleLeft, views[i].fov.angleRight);

			auto offset_y = (views[i].fov.angleDown + views[i].fov.angleUp) / 2;
			tan_center[i].y = angles_to_center(-view.y - e.y, views[i].fov.angleUp, views[i].fov.angleDown) + offset_y;
		}
	}

	for (int i = 0; i < 2; ++i)
	{
		const auto & fov = fovs[i];
		if (foveated_width < src[i].extent.w)
			fill_param_2d(tan_center[i].x, foveated_width, src[i].extent.w, params[i].x);
		else
			params[i].x = {uint16_t(src[i].extent.w)};
		if (foveated_height < src[i].extent.h)
			fill_param_2d(tan_center[i].y, foveated_height, src[i].extent.h, params[i].y);
		else
			params[i].y = {uint16_t(src[i].extent.h)};
	}
}

wivrn_foveation::wivrn_foveation(wivrn_vk_bundle & bundle, const xrt_hmd_parts & hmd) :
        foveated_width(hmd.screens[0].w_pixels / 2),
        foveated_height(hmd.screens[0].h_pixels),
        command_pool(bundle.device, vk::CommandPoolCreateInfo{.queueFamilyIndex = bundle.queue_family_index}),
        cmd(std::move(bundle.device.allocateCommandBuffers({
                .commandPool = command_pool,
                .commandBufferCount = 1,
        })[0])),
        host_buffer(
                bundle.device,
                {
                        .size = sizeof(render_compute_distortion_foveation_data),
                        .usage = vk::BufferUsageFlagBits::eTransferSrc,
                },
                VmaAllocationCreateInfo{
                        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                        .usage = VMA_MEMORY_USAGE_AUTO,
                })
{
}

void wivrn_foveation::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	std::lock_guard lock(mutex);

	const uint8_t orientation_ok = from_headset::tracking::orientation_valid | from_headset::tracking::orientation_tracked;

	views = tracking.views;

	std::optional<xrt_quat> head;

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device_id::HEAD)
			continue;

		if ((pose.flags & orientation_ok) != orientation_ok)
			return;

		head = xrt_cast(pose.pose.orientation);
		break;
	}

	if (!head.has_value())
		return;

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device_id::EYE_GAZE)
			continue;

		if ((pose.flags & orientation_ok) != orientation_ok)
			return;

		xrt_quat qgaze = xrt_cast(pose.pose.orientation);
		math_quat_unrotate(&qgaze, &head.value(), &gaze);
		break;
	}
}

std::array<to_headset::foveation_parameter, 2> wivrn_foveation::get_parameters()
{
	std::lock_guard lock(mutex);
	return params;
}

static void fill_ubo(
        uint32_t * ubo,
        const std::vector<uint16_t> & params,
        bool flip,
        size_t offset,
        size_t size,
        int count)
{
	assert(params.size() % 2 == 1);
	const int n_ratio = (params.size() - 1) / 2;
	*ubo = offset;
	if (flip)
		*ubo += size;
	for (auto [i, n]: std::ranges::enumerate_view(params))
	{
		const int n_source = std::abs(n_ratio - int(i)) + 1;
		for (size_t j = 0; j < n; ++j)
		{
			if (flip)
				ubo[1] = ubo[0] - n_source;
			else
				ubo[1] = ubo[0] + n_source;
			++ubo;
			--count;
		}
	}
	if (count > 0)
		std::fill(ubo + 1, ubo + count + 1, *ubo);
}

template <typename T>
static bool operator==(const T & a, const T & b)
{
	static_assert(std::has_unique_object_representations_v<T>);
	return std::memcmp(&a, &b, sizeof(T)) == 0;
}
bool operator==(const xrt_quat & a, const xrt_quat & b)
{
	return a.x == b.x and a.y == b.y and a.z == b.z and a.w == b.w;
}
bool operator==(const xrt_fov & a, const xrt_fov & b)
{
	return a.angle_left == b.angle_left and a.angle_right == b.angle_right and a.angle_up == b.angle_up and a.angle_down == b.angle_down;
}

vk::CommandBuffer wivrn_foveation::update_foveation_buffer(
        vk::Buffer target,
        bool flip_y,
        xrt_rect source[2],
        xrt_fov fovs[2])
{
	if (not target)
		return nullptr;

	if (target != gpu_buffer)
	{
		command_pool.reset();
		cmd.begin({});
		cmd.copyBuffer(host_buffer, target, vk::BufferCopy{.size = sizeof(render_compute_distortion_foveation_data)});
		cmd.end();
	}

	// Check if the last value is still valid
	if (last.flip_y == flip_y and last.src[0] == source[0] and last.src[1] == source[1] and last.fovs[0] == fovs[0])
	{
		std::lock_guard lock(mutex);
		if (last.gaze == gaze)
			return nullptr;
	}

	last = {
	        .gaze = gaze,
	        .flip_y = flip_y,
	        .src = {source[0], source[1]},
	        .fovs = {fovs[0], fovs[1]},
	};

	compute_params(source, fovs);

	auto ubo = (render_compute_distortion_foveation_data *)host_buffer.map();
	for (size_t view = 0; view < 2; ++view)
	{
		fill_ubo(ubo->x + view * RENDER_FOVEATION_BUFFER_DIMENSIONS, params[view].x, false, source[view].offset.w, source[view].extent.w, foveated_width);
		fill_ubo(ubo->y + view * RENDER_FOVEATION_BUFFER_DIMENSIONS, params[view].y, flip_y, source[view].offset.h, source[view].extent.h, foveated_height);
	}
	return cmd;
}
} // namespace wivrn
