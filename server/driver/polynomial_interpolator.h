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

template <int N, int polynomial_order = 2, int stored_samples = 10>
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

	XrDuration window;        // time span of samples to use for extrapolation
	XrDuration extrapolation; // maximum time difference between known sample and prediction
	float time_constant;      // duration (s) to convert velocities into positions

private:
	std::array<sample, stored_samples> data;

public:
	polynomial_interpolator(XrDuration window, XrDuration extrapolation, float time_constant) : window(window), extrapolation(extrapolation), time_constant(time_constant) {}

	void reset()
	{
		data.fill({});
	}

	void add_sample(const sample & s)
	{
		sample * min = nullptr;
		for (auto & i: data)
		{
			// Avoid samples too close to each other
			if (std::abs(i.timestamp - s.timestamp) < 2'000'000)
			{
				if (i.production_timestamp < s.production_timestamp)
					i = s;
				return;
			}
			if ((not min) or min->production_timestamp > i.production_timestamp)
				min = &i;
		}
		*min = s;
	}

	sample get_at(XrTime timestamp)
	{
		Eigen::Matrix<float, 2 * stored_samples, polynomial_order + 1> A;
		Eigen::Matrix<float, 2 * stored_samples, N> b;

		auto closest_sample = std::ranges::min_element(data, {}, [&](const sample & sample) {
			if (sample.production_timestamp == std::numeric_limits<XrTime>::lowest())
				return std::numeric_limits<XrTime>::max();
			else
				return std::abs(sample.timestamp - timestamp);
		});

		if (not closest_sample->y)
			return {};

		timestamp = std::min(timestamp, closest_sample->production_timestamp + extrapolation);

		XrTime production_timestamp = closest_sample->production_timestamp;

		int row = 0;
		for (const auto && [i, sample]: std::ranges::enumerate_view(data))
		{
			int abs_Δt = std::min<XrDuration>(std::abs(sample.timestamp - closest_sample->timestamp), std::numeric_limits<int>::max());

			if (abs_Δt > window or not sample.y.has_value())
				continue;

			production_timestamp = std::max(production_timestamp, sample.production_timestamp);

			float Δt = (sample.timestamp - timestamp) * 1.e-9;
			float Δtⁱ = 1;
			for (int i = 0; i <= polynomial_order; ++i)
			{
				A(row, i) = Δtⁱ;
				Δtⁱ *= Δt;
			}
			b.template block<1, N>(row, 0) = *sample.y;
			row++;

			if (sample.dy.has_value())
			{
				A(row, 0) = 0;

				Δtⁱ = 1;
				for (int i = 1; i <= polynomial_order; ++i)
				{
					A(row, i) = time_constant * i * Δtⁱ;
					Δtⁱ *= Δt;
				}
				b.template block<1, N>(row, 0) = *sample.dy * time_constant;
				row++;
			}
		}

		if (row == 0)
			return {};

		if (row == 1)
			return *closest_sample;

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
