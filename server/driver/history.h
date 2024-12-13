/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "clock_offset.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include <algorithm>
#include <cstddef>
#include <list>
#include <mutex>
#include <openxr/openxr.h>

namespace wivrn
{

template <typename Derived, typename Data, XrDuration extrapolation = 0, size_t MaxSamples = 10>
class history
{
	struct TimedData : public Data
	{
		XrTime produced_timestamp;
		XrTime at_timestamp_ns;
	};

	std::mutex mutex;
	std::list<TimedData> data;
	XrTime last_request;
	XrTime last_produced;

protected:
	history() :
	        last_request(os_monotonic_get_ns()) {}

	// return true if object is active (last request is not too old)
	bool add_sample(XrTime produced_timestamp, XrTime timestamp, const Data & sample, const clock_offset & offset)
	{
		XrTime produced = offset.from_headset(produced_timestamp);
		XrTime t = offset.from_headset(timestamp);
		std::lock_guard lock(mutex);

		bool active = produced - last_request < 1'000'000'000;
		last_produced = std::max(produced, last_produced);

		// Discard outdated data, packets could be reordered
		if (not data.empty())
		{
			// keep only one sample if the clock_offset is unreliable
			if (not offset)
			{
				U_LOG_T("not using history: clock_offset not stable");
				data.clear();
				data.emplace_back(sample, produced, t);
				return active;
			}

			if (data.back().produced_timestamp > produced)
				return active;
		}

		// Discard outdated predictions
		if (t != produced)
		{
			for (auto it = data.begin(); it != data.end();)
			{
				if (it->at_timestamp_ns == it->produced_timestamp // not a prediction
				    or it->produced_timestamp >= produced         // recent prediction
				    or it->at_timestamp_ns > t + 1'000'000)       // we don't have far enough data yet
					++it;
				else
					it = data.erase(it);
			}
		}

		// Insert the new sample
		auto it = std::lower_bound(data.begin(), data.end(), t, [](TimedData & sample, int64_t t) { return sample.at_timestamp_ns < t; });
		if (it == data.end())
			data.emplace_back(sample, produced, t);
		else if (it->at_timestamp_ns == t)
			*it = TimedData(sample, produced, t);
		else
			data.emplace(it, sample, produced, t);

		while (data.size() > MaxSamples)
			data.pop_front();

		return active;
	}

public:
	std::pair<std::chrono::nanoseconds, Data> get_at(XrTime at_timestamp_ns)
	{
		std::lock_guard lock(mutex);
		std::chrono::nanoseconds ex(std::max<XrTime>(0, at_timestamp_ns - last_produced));

		last_request = os_monotonic_get_ns();

		if (data.empty())
		{
			return {};
		}

		if (at_timestamp_ns - data.back().at_timestamp_ns > 1'000'000'000)
		{
			// stale data
			data.clear();
			return {};
		}

		if (data.size() == 1)
		{
			return {ex, data.front()};
		}

		if (data.front().at_timestamp_ns > at_timestamp_ns)
		{
			if constexpr (extrapolation)
			{
				auto second = data.begin();
				auto first = second++;
				at_timestamp_ns = std::max(at_timestamp_ns, data.front().at_timestamp_ns - extrapolation);
				return {ex, Derived::extrapolate(*first, *second, first->at_timestamp_ns, second->at_timestamp_ns, at_timestamp_ns)};
			}
			else
				return {ex, data.front()};
		}

		for (auto after = data.begin(), before = after++; after != data.end(); before = after++)
		{
			if (after->at_timestamp_ns > at_timestamp_ns)
			{
				float t = float(after->at_timestamp_ns - at_timestamp_ns) /
				          (after->at_timestamp_ns - before->at_timestamp_ns);
				return {ex, Derived::interpolate(*before, *after, t)};
			}
		}

		if constexpr (extrapolation)
		{
			auto prev = data.rbegin();
			auto last = prev++;
			at_timestamp_ns = std::min(at_timestamp_ns, data.back().at_timestamp_ns + extrapolation);
			return {ex, Derived::extrapolate(*prev, *last, prev->at_timestamp_ns, last->at_timestamp_ns, at_timestamp_ns)};
		}
		else
		{
			return {ex, data.back()};
		}
	}
};
} // namespace wivrn
