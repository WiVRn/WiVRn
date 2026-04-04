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

#include "foveation.h"

#include "driver/xrt_cast.h"
#include "utils/wivrn_vk_bundle.h"
#include "wivrn_packets.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_limits.h"

#include <array>
#include <cmath>
#include <ranges>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <openxr/openxr.h>

#define RENDER_FOVEATION_BUFFER_DIMENSIONS (4096 + 1)

namespace
{
struct ubo_data
{
	uint32_t x[XRT_MAX_VIEWS * RENDER_FOVEATION_BUFFER_DIMENSIONS];
	uint32_t y[XRT_MAX_VIEWS * RENDER_FOVEATION_BUFFER_DIMENSIONS];
	uint32_t alpha_width;
};

vk::raii::Sampler make_sampler(wivrn::vk_bundle & vk)
{
	vk::raii::Sampler res(
	        vk.device,
	        vk::SamplerCreateInfo{
	                .magFilter = vk::Filter::eLinear,
	                .minFilter = vk::Filter::eLinear,
	                .mipmapMode = vk::SamplerMipmapMode::eLinear,
	                .addressModeU = vk::SamplerAddressMode::eClampToBorder,
	                .addressModeV = vk::SamplerAddressMode::eClampToBorder,
	                .addressModeW = vk::SamplerAddressMode::eClampToBorder,
	                .borderColor = vk::BorderColor::eFloatOpaqueBlack,
	        });
	vk.name(res, "foveation sampler");
	return res;
}

vk::raii::DescriptorSetLayout make_ds_layout(wivrn::vk_bundle & vk)
{
	std::array bindings{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = 2,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 1,
	                .descriptorType = vk::DescriptorType::eStorageBuffer,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 2,
	                .descriptorType = vk::DescriptorType::eStorageImage,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 3,
	                .descriptorType = vk::DescriptorType::eStorageImage,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	};
	vk::raii::DescriptorSetLayout res{
	        vk.device,
	        vk::DescriptorSetLayoutCreateInfo{
	                .bindingCount = bindings.size(),
	                .pBindings = bindings.data(),
	        },
	};
	vk.name(*res, "foveation descriptor set layout");
	return res;
}

vk::raii::PipelineLayout make_layout(wivrn::vk_bundle & vk, vk::DescriptorSetLayout ds_layout)
{
	vk::raii::PipelineLayout res(vk.device,
	                             vk::PipelineLayoutCreateInfo{
	                                     .setLayoutCount = 1,
	                                     .pSetLayouts = &ds_layout,
	                             });
	vk.name(*res, "foveation pipeline layout");
	return res;
}

vk::raii::Pipeline make_pipeline(wivrn::vk_bundle & vk, vk::PipelineLayout layout)
{
	vk::raii::Pipeline res(
	        vk.device,
	        nullptr,
	        vk::ComputePipelineCreateInfo{
	                .stage = {
	                        .stage = vk::ShaderStageFlagBits::eCompute,
	                        .module = *vk.load_shader("foveation"),
	                        .pName = "main",
	                },
	                .layout = layout,
	        });
	vk.name(*res, "foveation pipeline");
	return res;
}

vk::raii::DescriptorPool make_ds_pool(wivrn::vk_bundle & vk)
{
	std::array pool_sizes{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = 2,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageImage,
	                .descriptorCount = 2,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageBuffer,
	                .descriptorCount = 1,
	        },
	};
	vk::raii::DescriptorPool res{
	        vk.device,
	        vk::DescriptorPoolCreateInfo{
	                .maxSets = 5,
	                .poolSizeCount = pool_sizes.size(),
	                .pPoolSizes = pool_sizes.data(),
	        },
	};
	vk.name(*res, "foveation descriptor pool");
	return res;
}
uint32_t divide_and_round_up(uint32_t a, uint32_t b)
{
	return (a + (b - 1)) / b;
}

} // namespace

// a, b: parameters computed by solve_foveation
// λ: pixel ratio between full size and foveated image (in range ]0,1[)
// c: coordinates in -1,1 range of the full size image where pixel ratio must be 1:1
// x: coordinates in -1,1 range of the foveated image
// result: full size image coordinates in -1,1 range
static double defoveate(double a, double b, double λ, double c, double x)
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

static bool is_zero_quat(xrt_quat q)
{
	return q.x == 0 and q.y == 0 and q.z == 0 and q.w == 0;
}

static xrt_vec2 yaw_pitch(xrt_quat q)
{
	if (is_zero_quat(q))
		return xrt_vec2{};

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
	e = tan(e);
	l = tan(l);
	r = tan(r);
	float res = std::clamp((e - l) / (r - l) * 2 - 1, -1.f, 1.f);
	// If the center isn't in the FoV, fallback to middle of image
	if (std::isnan(res))
		return 0;
	return res;
}

static float convergence_angle(float distance, float eye_x, float gaze_yaw)
{
	auto b = distance * std::sin(gaze_yaw) - eye_x;
	return std::asin(b / distance);
}

static void fill_param_2d(
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
		auto f = defoveate(a, b, scale, c, u);
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

void foveation::compute_params()
{
	auto e = yaw_pitch(gaze);

	if (manual_foveation.enabled)
		e.y = manual_foveation.pitch;

	for (size_t i = 0; i < 2; ++i)
	{
		const auto & fov = last.fovs[i];

		size_t extent_w = std::abs(last.src[i].extent.w);
		if (foveated_size.width < extent_w)
		{
			auto distance = manual_foveation.enabled ? manual_foveation.distance : convergence_distance;
			auto angle_x = convergence_angle(distance, eye_x[i], -e.x);
			auto center = angles_to_center(angle_x, fov.angle_left, fov.angle_right);
			fill_param_2d(center, foveated_size.width, extent_w, params[i].x);
		}
		else
			params[i].x = {uint16_t(last.src[i].extent.w)};

		size_t extent_h = std::abs(last.src[i].extent.h);
		if (foveated_size.height < extent_h)
		{
			auto angle_y = -e.y;
			if (is_zero_quat(gaze) and not manual_foveation.enabled)
			{
				// Natural gaze is not straight forward, adjust the angle
				angle_y += angle_offset;
			}
			auto center = angles_to_center(-angle_y, fov.angle_up, fov.angle_down);
			fill_param_2d(center, foveated_size.height, extent_h, params[i].y);
		}
		else
			params[i].y = {uint16_t(last.src[i].extent.h)};
	}
}

foveation::foveation(wivrn::vk_bundle & bundle, vk::Extent3D foveated_size) :
        foveated_size(foveated_size),
        // normal sight line is between 10° and 15° below horizontal
        // https://apps.dtic.mil/sti/tr/pdf/AD0758339.pdf pages 393-394
        // testing shows 10° looks better
        angle_offset(10 * M_PI / 180),
        convergence_distance(1 /* meter*/),
        gpu_buffer(
                bundle.device,
                {
                        .size = sizeof(ubo_data),
                        .usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
                },
                VmaAllocationCreateInfo{
                        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
                        .usage = VMA_MEMORY_USAGE_AUTO,
                },
                "foveation storage buffer"),
        sampler(make_sampler(bundle)),
        ds_layout(make_ds_layout(bundle)),
        layout(make_layout(bundle, ds_layout)),
        pipeline(make_pipeline(bundle, layout)),
        descriptor_pool(make_ds_pool(bundle)),
        descriptor_set(bundle.device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &*ds_layout,
        })[0]
                               .release())
{
	if (not(gpu_buffer.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		host_buffer = buffer_allocation(
		        bundle.device,
		        {
		                .size = gpu_buffer.info().size,
		                .usage = vk::BufferUsageFlagBits::eTransferSrc,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		        },
		        "foveation staging buffer");
	}

	bundle.name(descriptor_set, "foveation descriptor set");
}

void foveation::update_tracking(const from_headset::tracking & tracking)
{
	std::lock_guard lock(mutex);

	const uint8_t orientation_ok = from_headset::tracking::orientation_valid | from_headset::tracking::orientation_tracked;

	if (tracking.view_flags & XR_VIEW_STATE_POSITION_VALID_BIT)
	{
		eye_x[0] = tracking.views[0].pose.position.x;
		eye_x[1] = tracking.views[1].pose.position.x;
	}

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device_id::EYE_GAZE)
			continue;

		if ((pose.flags & orientation_ok) != orientation_ok)
			return;

		gaze = xrt_cast(pose.pose.orientation);
		return;
	}
}

void foveation::update_foveation_center_override(const from_headset::override_foveation_center & center)
{
	std::lock_guard lock(mutex);
	manual_foveation = center;
}

static void fill_ubo(
        std::span<uint32_t> ubo,
        const std::vector<uint16_t> & params,
        bool flip,
        size_t offset,
        size_t size,
        [[maybe_unused]] int count)
{
	assert(params.size() % 2 == 1);
	const int n_ratio = (params.size() - 1) / 2;
	ubo[0] = offset;
	if (flip)
		ubo[0] += size;
	for (auto [i, n]: std::ranges::enumerate_view(params))
	{
		const int n_source = std::abs(n_ratio - int(i)) + 1;
		for (size_t j = 0; j < n; ++j)
		{
			assert(count > 0);
			if (flip)
				ubo[1] = ubo[0] - n_source;
			else
				ubo[1] = ubo[0] + n_source;
			ubo = ubo.subspan(1);
			--count;
		}
	}
	if (not ubo.empty())
		std::ranges::fill(ubo, ubo[0]);
}

template <typename T>
static bool operator==(const T & a, const T & b)
{
	static_assert(std::has_unique_object_representations_v<T>);
	return std::memcmp(&a, &b, sizeof(T)) == 0;
}
static bool operator==(const xrt_quat & a, const xrt_quat & b)
{
	return a.x == b.x and a.y == b.y and a.z == b.z and a.w == b.w;
}
static bool operator==(const xrt_fov & a, const xrt_fov & b)
{
	return a.angle_left == b.angle_left and a.angle_right == b.angle_right and a.angle_up == b.angle_up and a.angle_down == b.angle_down;
}
void foveation::update_ubo(
        vk::raii::CommandBuffer & cmd,
        bool flip_y,
        std::array<xrt_rect, 2> src_rect,
        std::array<xrt_fov, 2> src_fov)
{
	// Check if the last value is still valid
	std::lock_guard lock(mutex);
	if (last.flip_y == flip_y and
	    last.src[0] == src_rect[0] and
	    last.src[1] == src_rect[1] and
	    last.fovs[0] == src_fov[0] and
	    last.fovs[1] == src_fov[1] and
	    (last.gaze == gaze or manual_foveation.enabled) and // Ignore the gaze if foveation center is overridden
	    std::abs(last.eye_x[0] - eye_x[0]) < 0.0005 and
	    std::abs(last.eye_x[1] - eye_x[1]) < 0.0005 and
	    last.manual_foveation.enabled == manual_foveation.enabled and
	    std::abs(last.manual_foveation.pitch - manual_foveation.pitch) < 0.0005 and
	    std::abs(last.manual_foveation.distance - manual_foveation.distance) < 0.0005)
		return;

	if (host_buffer)
		cmd.copyBuffer(host_buffer, gpu_buffer, vk::BufferCopy{.size = sizeof(ubo_data)});

	last = {
	        .gaze = gaze,
	        .flip_y = flip_y,
	        .src = {src_rect[0], src_rect[1]},
	        .fovs = {src_fov[0], src_fov[1]},
	        .eye_x = {eye_x[0], eye_x[1]},
	        .manual_foveation = manual_foveation,
	};

	compute_params();

	auto ubo = host_buffer ? host_buffer.data<ubo_data>() : gpu_buffer.data<ubo_data>();
	ubo->alpha_width = foveated_size.width / 2;
	for (size_t view = 0; view < 2; ++view)
	{
		bool flip = false;
		size_t offset, extent;

		if (src_rect[view].extent.w < 0)
		{
			flip = true;
			offset = src_rect[view].offset.w + src_rect[view].extent.w;
			extent = -src_rect[view].extent.w;
		}
		else
		{
			offset = src_rect[view].offset.w;
			extent = src_rect[view].extent.w;
		}
		fill_ubo(std::span(ubo->x + view * RENDER_FOVEATION_BUFFER_DIMENSIONS, RENDER_FOVEATION_BUFFER_DIMENSIONS),
		         params[view].x,
		         flip,
		         offset,
		         extent,
		         foveated_size.width);

		if (src_rect[view].extent.h < 0)
		{
			flip = not flip_y;
			offset = src_rect[view].offset.h + src_rect[view].extent.h;
			extent = -src_rect[view].extent.h;
		}
		else
		{
			flip = flip_y;
			offset = src_rect[view].offset.h;
			extent = src_rect[view].extent.h;
		}
		fill_ubo(std::span(ubo->y + view * RENDER_FOVEATION_BUFFER_DIMENSIONS, RENDER_FOVEATION_BUFFER_DIMENSIONS),
		         params[view].y,
		         flip,
		         offset,
		         extent,
		         foveated_size.height);
	}
}

std::array<to_headset::foveation_parameter, 2> foveation::foveate(
        vk::raii::Device & device,
        vk::raii::CommandBuffer & cmd,
        vk::ImageView y,
        vk::ImageView cbcr,
        bool flip_y,
        std::array<vk::ImageView, 2> src,
        std::array<xrt_rect, 2> src_rect,
        std::array<xrt_fov, 2> src_fov)
{
	update_ubo(cmd, flip_y, src_rect, src_fov);

	std::array src_image_info{
	        vk::DescriptorImageInfo{
	                .sampler = *sampler,
	                .imageView = src[0],
	                .imageLayout = vk::ImageLayout::eGeneral,
	        },
	        vk::DescriptorImageInfo{
	                .sampler = *sampler,
	                .imageView = src[1],
	                .imageLayout = vk::ImageLayout::eGeneral,
	        },
	};

	vk::DescriptorBufferInfo ubo_info{
	        .buffer = gpu_buffer,
	        .range = vk::WholeSize,
	};

	vk::DescriptorImageInfo y_info{
	        .imageView = y,
	        .imageLayout = vk::ImageLayout::eGeneral,
	};
	vk::DescriptorImageInfo cbcr_info{
	        .imageView = cbcr,
	        .imageLayout = vk::ImageLayout::eGeneral,
	};

	std::array writes = {
	        vk::WriteDescriptorSet{
	                .dstSet = descriptor_set,
	                .dstBinding = 0,
	                .descriptorCount = src_image_info.size(),
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .pImageInfo = src_image_info.data(),
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = descriptor_set,
	                .dstBinding = 1,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eStorageBuffer,
	                .pBufferInfo = &ubo_info,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = descriptor_set,
	                .dstBinding = 2,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eStorageImage,
	                .pImageInfo = &y_info,
	        },
	        vk::WriteDescriptorSet{
	                .dstSet = descriptor_set,
	                .dstBinding = 3,
	                .descriptorCount = 1,
	                .descriptorType = vk::DescriptorType::eStorageImage,
	                .pImageInfo = &cbcr_info,
	        },
	};

	device.updateDescriptorSets(writes, {});

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *layout, 0, descriptor_set, {});
	cmd.dispatch(divide_and_round_up(foveated_size.width, 8),
	             divide_and_round_up(foveated_size.height, 8),
	             2);

	return params;
}
} // namespace wivrn
