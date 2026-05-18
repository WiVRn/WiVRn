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
// Without WIVRN_USE_PERFETTO there is no consumer for the timestamps (the
// trace sinks are no-ops), so the pool stays disabled.
gpu_timestamp_pool::gpu_timestamp_pool([[maybe_unused]] wivrn::vk_bundle & vk,
                                       [[maybe_unused]] uint32_t queue_family_index,
                                       [[maybe_unused]] uint32_t slot_count,
                                       [[maybe_unused]] const std::string & name)
{
#ifdef WIVRN_USE_PERFETTO
	// Mirror the compile-time no-op when tracing is off at runtime: skip
	// the query pool entirely so we don't write/read timestamps every frame
	// only to drop them at the percetto category mask.
	if (!wivrn::trace::is_enabled())
		return;
	auto qprops = vk.physical_device.getQueueFamilyProperties();
	if (queue_family_index >= qprops.size() or qprops[queue_family_index].timestampValidBits == 0)
		return;
	assert(wivrn::trace::calibration_source() != nullptr);

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
	auto [res, ticks] = pool.getResults<uint64_t>(
	        slot * 2, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
	if (res != vk::Result::eSuccess or ticks[1] < ticks[0])
		return std::nullopt;
	std::array<uint64_t, 2> host_ns = {ticks[0], ticks[1]};
	if (vk_convert_timestamps_to_host_ns(cal, 2, host_ns.data()) != VK_SUCCESS)
		return std::nullopt;
	return sample{
	        .begin_ns = static_cast<int64_t>(host_ns[0]),
	        .end_ns = static_cast<int64_t>(host_ns[1]),
	        .frame_index = frame_indices[slot],
	};
#else
	(void)slot;
	return std::nullopt;
#endif
}
} // namespace wivrn
