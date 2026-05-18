/*
 * WiVRn VR streaming
 * Copyright (C) 2026  WiVRn Contributors
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

// Monado's vk_helpers.h pulls in xrt_vulkan_includes.h, which sets the
// VK_USE_PLATFORM_* defines *before* <vulkan/vulkan.h> gets included for the
// first time. Including it before wivrn_vk_bundle.h (which transitively pulls
// in vulkan_raii.hpp) ensures the platform-specific PFN_* surface types are
// declared and the function-pointer table in vk_helpers_h_funcs.h.inc compiles.
#ifdef WIVRN_USE_PERFETTO
#include "vk/vk_helpers.h"
#endif

#include "gpu_timestamp_pool.h"

#include "wivrn_trace.h"
#include "wivrn_vk_bundle.h"

#ifdef WIVRN_USE_PERFETTO
#include <array>
#include <cassert>
#endif

namespace wivrn
{
// No tracing build ⇒ no timestamp consumer; keep the pool disabled.
gpu_timestamp_pool::gpu_timestamp_pool([[maybe_unused]] wivrn::vk_bundle & vk,
                                       [[maybe_unused]] uint32_t queue_family_index,
                                       [[maybe_unused]] uint32_t slot_count,
                                       [[maybe_unused]] const std::string & name)
{
#ifdef WIVRN_USE_PERFETTO
	// Runtime tracing off: skip the pool instead of recording timestamps we'd drop.
	if (!wivrn::trace::is_enabled())
		return;
	auto qprops = vk.physical_device.getQueueFamilyProperties();
	if (queue_family_index >= qprops.size() or qprops[queue_family_index].timestampValidBits == 0)
		return;

	pool = vk.device.createQueryPool(vk::QueryPoolCreateInfo{
	        .queryType = vk::QueryType::eTimestamp,
	        .queryCount = slot_count * 2,
	});
	vk.name(pool, name);
	frame_indices.assign(slot_count, 0);
#endif
}

void gpu_timestamp_pool::cmd_begin(const vk::raii::CommandBuffer & cmd, uint8_t slot, uint64_t frame_index, vk::PipelineStageFlagBits2 stage)
{
	if (not *pool)
		return;
	cmd.resetQueryPool(*pool, slot * 2, 2);
	cmd.writeTimestamp2(stage, *pool, slot * 2);
	frame_indices[slot] = frame_index;
}

void gpu_timestamp_pool::cmd_end(const vk::raii::CommandBuffer & cmd, uint8_t slot, vk::PipelineStageFlagBits2 stage) const
{
	if (not *pool)
		return;
	cmd.writeTimestamp2(stage, *pool, slot * 2 + 1);
}

std::optional<gpu_timestamp_pool::sample> gpu_timestamp_pool::collect(uint8_t slot)
{
#ifdef WIVRN_USE_PERFETTO
	if (not *pool)
		return std::nullopt;
	::vk_bundle * cal = wivrn::trace::calibration_source();
	assert(cal != nullptr);
	// Read both queries into a stack array, no allocation.
	auto [res, times] = pool.getResult<std::array<uint64_t, 2>>(slot * 2, 2, sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
	if (res != vk::Result::eSuccess or times[1] < times[0])
		return std::nullopt;
	if (vk_convert_timestamps_to_host_ns(cal, times.size(), times.data()) != VK_SUCCESS)
		return std::nullopt;
	return sample{
	        .begin_ns = static_cast<int64_t>(times[0]),
	        .end_ns = static_cast<int64_t>(times[1]),
	        .frame_index = frame_indices[slot],
	};
#else
	(void)slot;
	return std::nullopt;
#endif
}
} // namespace wivrn
