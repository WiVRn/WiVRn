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
#include "util/u_logging.h"
#include <algorithm>
#include <cstddef>
#include <list>
#include <mutex>

template <typename Derived, typename Data, bool extrapolate = false, size_t MaxSamples = 10>
class history
{
	struct TimedData : public Data
	{
		XrTime produced_timestamp;
		XrTime at_timestamp_ns;
	};

	std::mutex mutex;
	std::list<TimedData> data;

protected:
	void add_sample(XrTime produced_timestamp, XrTime timestamp, const Data & sample, const clock_offset & offset)
	{
		XrTime produced = offset.from_headset(produced_timestamp);
		XrTime t = offset.from_headset(timestamp);
		std::lock_guard lock(mutex);

		// Discard outdated data, packets could be reordered
		if (not data.empty())
		{
			// keep only one sample if the clock_offset is unreliable
			if (not offset)
			{
				U_LOG_D("not using history: clock_offset not stable");
				data.clear();
				data.emplace_back(sample, produced, t);
				return;
			}

			if (data.back().produced_timestamp > produced)
				return;
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
		auto it = std::lower_bound(data.begin(), data.end(), t, [](TimedData & sample, uint64_t t) { return sample.at_timestamp_ns < t; });
		if (it == data.end())
			data.emplace_back(sample, produced, t);
		else if (it->at_timestamp_ns == t)
			*it = TimedData(sample, produced, t);
		else
			data.emplace(it, sample, produced, t);

		while (data.size() > MaxSamples)
			data.pop_front();
	}

public:
	std::pair<std::chrono::nanoseconds, Data> get_at(XrTime at_timestamp_ns)
	{
		std::lock_guard lock(mutex);
		std::chrono::nanoseconds ex(0);

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
			if constexpr (extrapolate)
			{
				auto second = data.begin();
				auto first = second++;
				return {ex, Derived::extrapolate(*first, *second, first->at_timestamp_ns, second->at_timestamp_ns, at_timestamp_ns)};
			}
			else
				return {ex, data.front()};
		}

		for (auto after = data.begin(), before = after++; after != data.end(); before = after++)
		{
			if (after->at_timestamp_ns > at_timestamp_ns)
			{
				ex = std::chrono::nanoseconds(at_timestamp_ns - std::min(before->produced_timestamp, after->produced_timestamp));
				float t = float(after->at_timestamp_ns - at_timestamp_ns) /
				          (after->at_timestamp_ns - before->at_timestamp_ns);
				return {ex, Derived::interpolate(*before, *after, t)};
			}
		}

		ex = std::chrono::nanoseconds(at_timestamp_ns - data.back().produced_timestamp);
		if constexpr (extrapolate)
		{
			auto prev = data.rbegin();
			auto last = prev++;
			return {ex, Derived::extrapolate(*prev, *last, prev->at_timestamp_ns, last->at_timestamp_ns, at_timestamp_ns)};
		}
		else
		{
			return {ex, data.back()};
		}
	}
};
