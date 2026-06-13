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

// Perfetto tracing via Percetto. No-op unless built with WIVRN_USE_PERFETTO.

// Monado's vk_bundle (C struct from util/vk/vk_helpers.h); used as the
// calibration source for converting GPU timestamp-query ticks to host
// monotonic ns via vk_convert_timestamps_to_host_ns.
struct vk_bundle;

namespace wivrn::trace
{
// Idempotent. No-op without WIVRN_USE_PERFETTO or WIVRN_TRACING.
void init();

// True iff WIVRN_TRACING is set in the environment (and WIVRN_USE_PERFETTO
// is enabled). Cached after the first read.
bool is_enabled();

// Set by the compositor after vk_init_from_given so query-pool ticks can be
// converted via monado's calibration cache. Single-threaded init window;
// reads happen on encoder threads after streaming starts.
void set_calibration_source(::vk_bundle * monado_vk);
::vk_bundle * calibration_source();

// Instant on the feedback track at an explicit monotonic timestamp.
// name must be a string literal (storage outlives the emit).
void instant_feedback(const char * name, int64_t time_ns, uint64_t frame, uint8_t stream);

// GPU-hardware-timestamped slice on the WiVRn encoder track.
// begin_ns/end_ns must come from GPU query pools (gpu_timestamp_pool::collect),
// already converted to host monotonic via calibrated timestamps.
enum class gpu_track
{
	vulkan_encode,
	vulkan_image_copy,
	vulkan_host_copy,
	vulkan_host_copy_overflow,
	nvenc_copy,
	va_copy,
};
void gpu_slice(gpu_track which, const char * slice_name, int64_t begin_ns, int64_t end_ns, uint64_t frame, uint8_t stream);

enum class cpu_track
{
	encoder,
	compositor,
	network,
};

// RAII CPU span. Zero-cost when tracing is off. Construct as a local with a
// chosen variable name; the span closes at scope exit.
class scope
{
	cpu_track which;
	uint8_t stream;
	uint64_t frame;
	const char * name;
	int64_t begin_ns;
	bool active;

public:
	scope(cpu_track which, uint8_t stream, uint64_t frame, const char * name);
	~scope();
	scope(const scope &) = delete;
	scope & operator=(const scope &) = delete;
};

void cpu_instant(cpu_track which, const char * name, uint64_t frame, uint8_t stream);

// Non-RAII begin/end pair, for spans whose lifetime crosses function calls
// (e.g. video_encoder::SendData where a frame is split across several calls).
// name must point to storage that outlives both the begin and end call.
void cpu_begin(cpu_track which, uint8_t stream, uint64_t frame, const char * name);
void cpu_end(cpu_track which, uint8_t stream, uint64_t frame, const char * name);
} // namespace wivrn::trace

#ifdef WIVRN_USE_PERFETTO

#include <percetto.h>

#define WIVRN_PERCETTO_CATEGORIES(C, G)            \
	C(encoder, "WiVRn encoder CPU spans")      \
	C(encoder_gpu, "WiVRn encoder GPU slices") \
	C(compositor, "WiVRn compositor spans")    \
	C(network, "WiVRn network send spans")     \
	C(feedback, "WiVRn dump_time markers")

PERCETTO_CATEGORY_DECLARE(WIVRN_PERCETTO_CATEGORIES)

PERCETTO_TRACK_DECLARE(wivrn_feedback);
PERCETTO_TRACK_DECLARE(wivrn_encoder_cpu);
PERCETTO_TRACK_DECLARE(wivrn_compositor_cpu);
PERCETTO_TRACK_DECLARE(wivrn_network_cpu);

#endif // WIVRN_USE_PERFETTO
