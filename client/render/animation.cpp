/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "animation.h"
#include "render/scene_components.h"

#include <algorithm>
#include <cmath>
#include <entt/entt.hpp>
#include <glm/ext/quaternion_geometric.hpp>

namespace
{

glm::vec3 interp(const glm::vec3 & a, const glm::vec3 & b, float t)
{
	return a + t * (b - a);
}

glm::quat interp(const glm::quat & a, const glm::quat & b, float t)
{
	float d = glm::dot(a, b);

	if (d > 0.99999)
	{
		return a + t * (b - a);
	}
	else
	{
		float θ = std::acos(std::abs(d));

		float sinθ = std::sin(θ);
		float sinθt = std::sin(θ * t);
		float sinθ1_t = std::sin(θ * (1 - t));

		return sinθ1_t / sinθ * a + std::copysign(sinθt / sinθ, d) * b;
	}
}

glm::vec3 cubic_spline(const glm::vec3 & a, const glm::vec3 & va, const glm::vec3 & vb, const glm::vec3 & b, float t, float td)
{
	float t2 = t * t;
	float t3 = t2 * t;

	return (2 * t3 - 3 * t2 + 1) * a +
	       td * (t3 - 2 * t2 + t) * va +
	       (3 * t2 - 2 * t3) * b +
	       td * (t3 - t2) * vb;
}

glm::quat cubic_spline(const glm::quat & a, const glm::quat & vb, const glm::quat & va, const glm::quat & b, float t, float td)
{
	float t2 = t * t;
	float t3 = t2 * t;

	return glm::normalize((2 * t3 - 3 * t2 + 1) * a +
	                      td * (t3 - 2 * t2 + t) * vb +
	                      (3 * t2 - 2 * t3) * b +
	                      td * (t3 - t2) * va);
}

template <auto components::node::*Field>
void apply_track(entt::registry & scene, const components::animation_track_impl<Field> & track, float current_time)
{
	entt::entity target = track.target;
	auto interpolation = track.interpolation;
	auto field = track.field;
	auto & value = scene.get<components::node>(track.target).*field;

	// Find the first keyframe with a timestamp strictly greater than the current time
	size_t k = std::ranges::upper_bound(track.timestamp, current_time) - track.timestamp.begin();
	if (k == 0)
	{
		value = track.value[0];
		return;
	}

	switch (interpolation)
	{
		case components::animation_track_base::interpolation_t::step:
			value = track.value[k - 1];
			break;

		case components::animation_track_base::interpolation_t::linear:
			if (k == track.timestamp.size())
				value = track.value[k - 1];
			else
			{
				float t = (current_time - track.timestamp[k - 1]) / (track.timestamp[k] - track.timestamp[k - 1]);
				const auto & a = track.value[k - 1];
				const auto & b = track.value[k];
				value = interp(a, b, t);
			}
			break;

		case components::animation_track_base::interpolation_t::cubic_spline:
			if (k == track.timestamp.size())
				value = track.value[k - 1];
			else
			{
				float t = (current_time - track.timestamp[k - 1]) / (track.timestamp[k] - track.timestamp[k - 1]);
				const auto & a = track.value[3 * k - 2];  // v_k in gltf spec
				const auto & vb = track.value[3 * k - 1]; // b_k
				const auto & va = track.value[3 * k];     // a_k+1
				const auto & b = track.value[3 * k + 1];  // v_k+1

				value = cubic_spline(a, va, vb, b, t, track.timestamp[k] - track.timestamp[k - 1]);
			}
			break;
	}
}
} // namespace

void renderer::animate(entt::registry & scene, float dt)
{
	for (auto && [entity, animation]: scene.view<components::animation>().each())
	{
		if (not animation.playing)
			continue;

		animation.current_time += dt;
		if (animation.current_time > animation.duration)
		{
			if (animation.looping)
				animation.current_time = std::fmod(animation.current_time, animation.duration);
			else
				animation.current_time = animation.duration;
		}
		else if (animation.current_time < 0)
			animation.current_time = 0;

		for (const auto & track: animation.tracks)
			std::visit([&](auto & track) { apply_track(scene, track, animation.current_time); }, track);
	}
}
