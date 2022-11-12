// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "wivrn_session.h"
#include <algorithm>
#include <mutex>
#include <vector>

template <typename Data, size_t MaxSamples = 10> class history
{
	struct TimedData : public Data
	{
		TimedData(const Data &d, uint64_t t) : Data(d), at_timestamp_ns(t) {}
		uint64_t at_timestamp_ns;
	};

	std::mutex mutex;
	std::vector<TimedData> data;

protected:
	void
	add_sample(uint64_t timestamp, const Data &sample, const clock_offset &offset)
	{
		std::lock_guard lock(mutex);

		uint64_t t = offset.from_headset(timestamp);
		auto it = std::lower_bound(data.begin(), data.end(), t,
		                           [](TimedData &sample, uint64_t t) { return sample.at_timestamp_ns < t; });

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
	Data
	get_at(uint64_t at_timestamp_ns)
	{
		std::lock_guard lock(mutex);

		if (data.empty()) {
			return {};
		}

		if (data.size() == 1) {
			return data[0];
		}

		if (data.front().at_timestamp_ns > at_timestamp_ns) {
			return extrapolate(data[0], data[1], data[0].at_timestamp_ns, data[1].at_timestamp_ns,
			                   at_timestamp_ns);
		}

		for (size_t i = 1; i < data.size(); ++i) {
			if (data[i].at_timestamp_ns > at_timestamp_ns) {
				float t = float(data[i].at_timestamp_ns - at_timestamp_ns) /
				          (data[i].at_timestamp_ns - data[i - 1].at_timestamp_ns);
				return interpolate(data[i - 1], data[i], t);
			}
		}

		const auto &d0 = data[data.size() - 2];
		const auto &d1 = data.back();

		return extrapolate(d0, d1, d0.at_timestamp_ns, d1.at_timestamp_ns, at_timestamp_ns);
	}
};

xrt_space_relation
interpolate(const xrt_space_relation &a, const xrt_space_relation &b, float t);

xrt_space_relation
extrapolate(const xrt_space_relation &a, const xrt_space_relation &b, uint64_t ta, uint64_t tb, uint64_t t);

class pose_list : public history<xrt_space_relation>
{
	device_id device;

public:
	pose_list(device_id id) : device(id) {}

	void
	update_tracking(const from_headset::tracking &, const clock_offset &offset);

	static xrt_space_relation
	convert_pose(const from_headset::tracking::pose &);
};
