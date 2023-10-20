/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "offset_estimator.h"

#include "clock_offset.h"
#include "util/u_logging.h"
#include "wivrn_packets.h"

static const double lowpass = 0.8;
static const double max_rtt_ratio = 3;

namespace xrt::drivers::wivrn
{

// We need to estimate the time offset with the headset,
// We first send a packet with current time t0, headset fills its current time t1 when it processes it then sends it back. We receive it back ad t2
// Normal estimate is that PC time is (t1+t2)/2 when headset processes it, however due to asymmetrical network load this is not what is observed.

// We try to estimate the reception time as t = t2 + (t1 - t2) * x
// U (input vector) is t0, t1, t2
// filtered_U is lowpass filtered
// The value we want to minimize is the variation of offset

clock_offset offset_estimator::get_offset(const from_headset::timesync_response & packet, int64_t now, const clock_offset old_offset)
{
	int64_t rtt = now - packet.query.count();
	Eigen::Vector3d U(packet.query.count(), packet.response, now);

	// initial assumption: latency is symmetrical
	if (filtered_U == Eigen::Vector3d::Zero())
	{
		filtered_U = U;
		int64_t offset = filtered_U[1] - 0.5 * (filtered_U[0] + filtered_U[2]);
		return clock_offset{std::chrono::nanoseconds(offset)};
	}

	auto mean_rtt = filtered_U[2] - filtered_U[0];
	filtered_U = filtered_U + lowpass * (U - filtered_U);
	// Packet took too long, probably a retransmit so skip it
	// as we don't know on which way it was retransmitted
	if (rtt > max_rtt_ratio * mean_rtt)
	{
		U_LOG_D("skip packet with RTT %ldms", rtt / 1000000);
		return old_offset;
	}

	auto tmp = U - filtered_U;
	A = A * 0.99 + tmp * tmp.transpose();

	auto x = (A(0,1) - A(0,2) - A(1,2) + A(2,2)) / (A(0,0) - 2*A(0,2) + A(2,2));
	x = std::clamp(x, 0., 1.);

	int64_t t = std::lerp(now, packet.query.count(), x);
	int64_t offset = packet.response - t;
	U_LOG_D("offset estimator x=%f offset diff %ldµs", x, (old_offset.epoch_offset.count() - offset)/1000);
	if (old_offset.epoch_offset.count() != 0)
	{
		offset = std::lerp(offset, old_offset.epoch_offset.count(), lowpass);
	}
	return clock_offset{std::chrono::nanoseconds(offset)};
}

}
