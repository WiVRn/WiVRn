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

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{
struct vk_bundle;

// A begin/end GPU timestamp pair per slot, used to measure GPU-side encode and
// copy durations for tracing. Conversion to host monotonic ns goes through
// monado's vk_convert_timestamps_to_host_ns (via wivrn::trace::calibration_source)
// so the calibration cache is shared with any future monado-side perfetto
// producer. If runtime tracing is off, the calibration source is missing, or
// the queue family doesn't support timestamps, the pool stays disabled and
// every method is a no-op. Default-constructed instances are also disabled,
// so members can be declared before being assigned.
class gpu_timestamp_pool
{
	vk::raii::QueryPool pool = nullptr;
	std::vector<uint64_t> frame_indices;

public:
	gpu_timestamp_pool() = default;
	gpu_timestamp_pool(wivrn::vk_bundle & vk, uint32_t queue_family_index, uint32_t slot_count, const std::string & name);

	void cmd_begin(const vk::raii::CommandBuffer & cmd, uint8_t slot, uint64_t frame_index, vk::PipelineStageFlagBits2 stage);
	void cmd_end(const vk::raii::CommandBuffer & cmd, uint8_t slot, vk::PipelineStageFlagBits2 stage) const;

	struct sample
	{
		int64_t begin_ns;
		int64_t end_ns;
		uint64_t frame_index;
	};
	std::optional<sample> collect(uint8_t slot);
};
} // namespace wivrn
