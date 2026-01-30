/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

// xlib sucks
#ifdef Success
#undef Success
#endif

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <Eigen/QR>
#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>

#include "openxr/openxr.h"
#include "wivrn_config.h"

template <int N, bool quaternion = false, int polynomial_order = 2, int stored_samples = 30>
class polynomial_interpolator
{
public:
	using value_type = Eigen::Vector<float, N>;

	struct sample
	{
		XrTime production_timestamp = std::numeric_limits<XrTime>::lowest();
		XrTime timestamp;
		std::optional<value_type> y;
		std::optional<value_type> dy;
	};

	XrDuration window = 30'000'000; // time span of samples to use for extrapolation
	float time_constant = 0.001;    // duration (s) to convert velocities into positions

private:
	std::array<sample, stored_samples> data;

public:
	polynomial_interpolator() = default;
	polynomial_interpolator(XrDuration window, float time_constant) : window(window), time_constant(time_constant) {}

	void reset()
	{
		data.fill({});
	}

	void add_sample(const sample & s)
	{
		sample * oldest = nullptr;
		for (auto & i: data)
		{
			// Avoid samples too close to each other
			if (std::abs(i.timestamp - s.timestamp) < 2'000'000)
			{
				if (i.production_timestamp < s.production_timestamp)
					i = s;
				return;
			}
			if ((not oldest) or oldest->production_timestamp > i.production_timestamp)
				oldest = &i;
		}
		*oldest = s;
		if constexpr (quaternion)
		{
			if (s.y)
			{
				sample * closest = nullptr;
				for (auto & i: data)
				{
					if (&i == oldest or not i.y)
						continue;
					if (not closest)
						closest = &i;
					else if (std::abs(closest->timestamp - s.timestamp) > std::abs(i.timestamp - s.timestamp))
						closest = &i;
				}
				if (closest and closest->y->dot(*s.y) < 0)
					*oldest->y *= -1;
			}
		}
	}

	sample get_at(XrTime timestamp)
	{
		Eigen::Matrix<float, 2 * stored_samples, polynomial_order + 1> A;
		Eigen::Matrix<float, 2 * stored_samples, N> b;

		const XrTime production_timestamp = std::ranges::max(data | std::ranges::views::transform(&sample::production_timestamp));
		// Maximum is the minimum of now + max_extrapolation_ns (outside of this function)
		// and production_ts + 1.1 * max_extrapolation_ns
		// This allows a small buffer so that polynomial extrapolation fills the gap of networking hiccups
		timestamp = std::min(timestamp, production_timestamp + (wivrn::max_extrapolation_ns * 11) / 10);

		int row = 0;
		for (const auto && [i, sample]: std::ranges::enumerate_view(data))
		{
			if (not sample.y)
				continue;

			int abs_Δt = std::abs(sample.timestamp - timestamp);

			float weight = 1. / (1. + std::pow(abs_Δt / float(window), 3.));

			float Δt = (sample.timestamp - timestamp) * 1.e-9;
			float Δtⁱ = 1;
			for (int i = 0; i <= polynomial_order; ++i)
			{
				A(row, i) = weight * Δtⁱ;
				Δtⁱ *= Δt;
			}
			b.template block<1, N>(row, 0) = *sample.y * weight;
			row++;

			if (sample.dy.has_value())
			{
				A(row, 0) = 0;

				Δtⁱ = 1;
				for (int i = 1; i <= polynomial_order; ++i)
				{
					A(row, i) = weight * time_constant * i * Δtⁱ;
					Δtⁱ *= Δt;
				}
				b.template block<1, N>(row, 0) = *sample.dy * weight * time_constant;
				row++;
			}
		}

		// Not enough data to extrapolate
		if (row < 2)
		{
			auto closest = std::ranges::min_element(data, std::less{}, [&](const auto & sample) {
				if (not sample.y)
					return std::numeric_limits<XrDuration>::max();
				return std::abs(sample.timestamp - timestamp);
			});
			if (closest->y and std::abs(closest->production_timestamp - timestamp) < 1'000'000'000)
				return *closest;
			return {};
		}

		auto Aprime = A.block(0, 0, row, polynomial_order + 1);
		auto bprime = b.block(0, 0, row, N);

		Eigen::Matrix<float, N, polynomial_order + 1> sol;

		if (row <= polynomial_order)
			// Underdetermined case
			sol = Eigen::ColPivHouseholderQR<decltype(Aprime)>{Aprime}.solve(bprime).transpose();
		else
			sol = (Aprime.transpose() * Aprime).ldlt().solve(Aprime.transpose() * bprime).transpose();

		return sample{
		        .production_timestamp = production_timestamp,
		        .timestamp = timestamp,
		        .y = sol.template block<N, 1>(0, 0),
		        .dy = sol.template block<N, 1>(0, 1),
		};
	}
};
