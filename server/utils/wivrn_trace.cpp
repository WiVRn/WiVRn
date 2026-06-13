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

#include "wivrn_trace.h"

#ifdef WIVRN_USE_PERFETTO

#include "os/os_time.h"
#include "util/u_debug.h"
#include "util/u_logging.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>

PERCETTO_CATEGORY_DEFINE(WIVRN_PERCETTO_CATEGORIES)

PERCETTO_TRACK_DEFINE(wivrn_feedback, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(wivrn_encoder_cpu, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(wivrn_compositor_cpu, PERCETTO_TRACK_EVENTS);
PERCETTO_TRACK_DEFINE(wivrn_network_cpu, PERCETTO_TRACK_EVENTS);

DEBUG_GET_ONCE_BOOL_OPTION(wivrn_tracing, "WIVRN_TRACING", false)

namespace wivrn::trace
{
namespace
{
// init() runs once during server startup before any streaming/encoding
// thread exists, so a plain bool suffices: writers and readers are
// serialized by program structure, not by atomics.
bool initialized = false;
::vk_bundle * calibration = nullptr;

// Perfetto's default socket is root-only; use per-user path so user-launched traced can bind.
// setenv(..., 0) preserves any caller-supplied override.
std::string default_producer_sock_path()
{
	if (const char * xdg = std::getenv("XDG_RUNTIME_DIR"); xdg and *xdg)
		return std::string(xdg) + "/wivrn-perfetto-producer.sock";
	return "/tmp/wivrn-perfetto-producer.sock";
}

percetto_category * domain_category(cpu_track d)
{
	switch (d)
	{
		case cpu_track::encoder:
			return &g_percetto_category_encoder;
		case cpu_track::compositor:
			return &g_percetto_category_compositor;
		case cpu_track::network:
			return &g_percetto_category_network;
	}
	return &g_percetto_category_encoder;
}

uint32_t category_mask(percetto_category * cat)
{
	return atomic_load_explicit(&cat->sessions, memory_order_relaxed);
}

percetto_track * domain_cpu_track(cpu_track d)
{
	switch (d)
	{
		case cpu_track::encoder:
			return I_PERCETTO_TRACK_PTR(wivrn_encoder_cpu);
		case cpu_track::compositor:
			return I_PERCETTO_TRACK_PTR(wivrn_compositor_cpu);
		case cpu_track::network:
			return I_PERCETTO_TRACK_PTR(wivrn_network_cpu);
	}
	return I_PERCETTO_TRACK_PTR(wivrn_encoder_cpu);
}

void emit_data_event(percetto_category * cat, uint32_t mask, int32_t type, percetto_track * trk, const char * name, int64_t ts, uint64_t frame, uint8_t stream)
{
	percetto_event_ext_data(cat, mask, name, type, trk, (uint64_t)ts, PERCETTO_UINT("frame", frame), PERCETTO_UINT("stream", stream), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE(), I_PERCETTO_DBG_NONE());
}

void emit_begin_end_pair(cpu_track which, const char * name, int64_t begin_ns, int64_t end_ns, uint64_t frame, uint8_t stream)
{
	percetto_category * cat = domain_category(which);
	const uint32_t mask = category_mask(cat);
	if (!mask)
		return;
	percetto_track * trk = domain_cpu_track(which);
	emit_data_event(cat, mask, PERCETTO_EVENT_BEGIN, trk, name, begin_ns, frame, stream);
	percetto_event_with_args(cat, mask, nullptr, PERCETTO_EVENT_END, trk, 0, (uint64_t)end_ns);
}
} // namespace

void init()
{
	assert(!initialized);
	if (!debug_get_bool_option_wivrn_tracing())
	{
		U_LOG_D("wivrn::trace: WIVRN_TRACING not set, percetto tracing disabled");
		return;
	}

	const auto sock = default_producer_sock_path();
	if (setenv("PERFETTO_PRODUCER_SOCK_NAME", sock.c_str(), 0) == 0)
		U_LOG_I("wivrn::trace: producer socket %s", std::getenv("PERFETTO_PRODUCER_SOCK_NAME"));

	// Must match os_monotonic_get_ns() used for all explicit timestamps.
	const int rc = PERCETTO_INIT(PERCETTO_CLOCK_MONOTONIC);
	if (rc != 0)
	{
		U_LOG_E("wivrn::trace: PERCETTO_INIT failed: %d", rc);
		return;
	}
	PERCETTO_REGISTER_TRACK(wivrn_feedback);
	// Eager registration ensures descriptors precede events; tracks appear
	// under wivrn-server rather than as global orphans.
	// PERCETTO_TRACK_DEFINE sets .name to the token string; patch first.
	I_PERCETTO_TRACK_PTR(wivrn_encoder_cpu)->name = "WiVRn encoder";
	I_PERCETTO_TRACK_PTR(wivrn_compositor_cpu)->name = "WiVRn compositor";
	I_PERCETTO_TRACK_PTR(wivrn_network_cpu)->name = "WiVRn network";
	PERCETTO_REGISTER_TRACK(wivrn_encoder_cpu);
	PERCETTO_REGISTER_TRACK(wivrn_compositor_cpu);
	PERCETTO_REGISTER_TRACK(wivrn_network_cpu);
	initialized = true;
	U_LOG_I("wivrn::trace: init complete");
}

bool is_enabled()
{
	return debug_get_bool_option_wivrn_tracing();
}

void set_calibration_source(::vk_bundle * monado_vk)
{
	calibration = monado_vk;
}

::vk_bundle * calibration_source()
{
	return calibration;
}

void instant_feedback(const char * name, int64_t time_ns, uint64_t frame, uint8_t stream)
{
	if (!initialized)
		return;
	TRACE_EVENT_BEGIN_ON_TRACK_DATA(feedback, wivrn_feedback, time_ns, name, PERCETTO_UINT("frame", frame), PERCETTO_UINT("stream", stream));
	TRACE_EVENT_END_ON_TRACK(feedback, wivrn_feedback, time_ns);
}

void gpu_slice(gpu_track which, const char * slice_name, int64_t begin_ns, int64_t end_ns, uint64_t frame, uint8_t stream)
{
	(void)which;
	if (!initialized)
		return;
	percetto_category * cat = &g_percetto_category_encoder_gpu;
	const uint32_t mask = category_mask(cat);
	if (!mask)
		return;
	percetto_track * trk = I_PERCETTO_TRACK_PTR(wivrn_encoder_cpu);
	emit_data_event(cat, mask, PERCETTO_EVENT_BEGIN, trk, slice_name, begin_ns, frame, stream);
	percetto_event_with_args(cat, mask, nullptr, PERCETTO_EVENT_END, trk, 0, (uint64_t)end_ns);
}

scope::scope(cpu_track which, uint8_t stream, uint64_t frame, const char * name) :
        which(which), stream(stream), frame(frame), name(name), begin_ns(0), active(false)
{
	if (!initialized)
		return;
	if (!category_mask(domain_category(which)))
		return;
	active = true;
	begin_ns = os_monotonic_get_ns();
}

scope::~scope()
{
	if (!active)
		return;
	emit_begin_end_pair(which, name, begin_ns, os_monotonic_get_ns(), frame, stream);
}

void cpu_instant(cpu_track which, const char * name, uint64_t frame, uint8_t stream)
{
	if (!initialized)
		return;
	percetto_category * cat = domain_category(which);
	const uint32_t mask = category_mask(cat);
	if (!mask)
		return;
	percetto_track * trk = domain_cpu_track(which);
	emit_data_event(cat, mask, PERCETTO_EVENT_INSTANT, trk, name, os_monotonic_get_ns(), frame, stream);
}

void cpu_begin(cpu_track which, uint8_t stream, uint64_t frame, const char * name)
{
	if (!initialized)
		return;
	percetto_category * cat = domain_category(which);
	const uint32_t mask = category_mask(cat);
	if (!mask)
		return;
	percetto_track * trk = domain_cpu_track(which);
	emit_data_event(cat, mask, PERCETTO_EVENT_BEGIN, trk, name, os_monotonic_get_ns(), frame, stream);
}

void cpu_end(cpu_track which, uint8_t stream, uint64_t frame, const char * name)
{
	(void)name;
	(void)frame;
	(void)stream;
	if (!initialized)
		return;
	percetto_category * cat = domain_category(which);
	const uint32_t mask = category_mask(cat);
	if (!mask)
		return;
	percetto_track * trk = domain_cpu_track(which);
	percetto_event_with_args(cat, mask, nullptr, PERCETTO_EVENT_END, trk, 0, (uint64_t)os_monotonic_get_ns());
}
} // namespace wivrn::trace

#else // !WIVRN_USE_PERFETTO

namespace wivrn::trace
{
void init() {}
bool is_enabled()
{
	return false;
}
void set_calibration_source(::vk_bundle *) {}
::vk_bundle * calibration_source()
{
	return nullptr;
}
void instant_feedback(const char *, int64_t, uint64_t, uint8_t) {}
void gpu_slice(gpu_track, const char *, int64_t, int64_t, uint64_t, uint8_t) {}
scope::scope(cpu_track which, uint8_t stream, uint64_t frame, const char * name) :
        which(which), stream(stream), frame(frame), name(name), begin_ns(0), active(false) {}
scope::~scope() {}
void cpu_instant(cpu_track, const char *, uint64_t, uint8_t) {}
void cpu_begin(cpu_track, uint8_t, uint64_t, const char *) {}
void cpu_end(cpu_track, uint8_t, uint64_t, const char *) {}
} // namespace wivrn::trace

#endif // WIVRN_USE_PERFETTO
