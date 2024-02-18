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

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include <algorithm>
#include "clock_offset.h"

template <typename Derived, typename Data, size_t MaxSamples = 10>
class history
{
	struct TimedData : public Data
	{
		TimedData(const Data & d, uint64_t t) :
		        Data(d), at_timestamp_ns(t) {}
		uint64_t at_timestamp_ns;
	};

	std::mutex mutex;
	std::vector<TimedData> data;

protected:
	void add_sample(uint64_t timestamp, const Data & sample, const clock_offset & offset)
	{
		std::lock_guard lock(mutex);

		uint64_t t = offset.from_headset(timestamp);
		auto it = std::lower_bound(data.begin(), data.end(), t, [](TimedData & sample, uint64_t t) { return sample.at_timestamp_ns < t; });

		if (it == data.end())
			data.emplace_back(sample, t);
		else if (it->at_timestamp_ns == t)
			*it = TimedData(sample, t);
		else
			data.emplace(it, sample, t);

		if (data.size() > MaxSamples)
			data.erase(data.begin());
	}

public:
	Data get_at(uint64_t at_timestamp_ns)
	{
		std::lock_guard lock(mutex);

		if (data.empty())
		{
			return {};
		}

		if (data.size() == 1)
		{
			return data[0];
		}

		if (data.front().at_timestamp_ns > at_timestamp_ns)
		{
			return Derived::extrapolate(data[0], data[1], data[0].at_timestamp_ns, data[1].at_timestamp_ns, at_timestamp_ns);
		}

		for (size_t i = 1; i < data.size(); ++i)
		{
			if (data[i].at_timestamp_ns > at_timestamp_ns)
			{
				float t = float(data[i].at_timestamp_ns - at_timestamp_ns) /
				          (data[i].at_timestamp_ns - data[i - 1].at_timestamp_ns);
				return Derived::interpolate(data[i - 1], data[i], t);
			}
		}

		const auto & d0 = data[data.size() - 2];
		const auto & d1 = data.back();

		return Derived::extrapolate(d0, d1, d0.at_timestamp_ns, d1.at_timestamp_ns, at_timestamp_ns);
	}
};
