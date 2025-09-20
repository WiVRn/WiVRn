/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <bit>
#include <boost/pfr/core.hpp>
#include <type_traits>
#include <vulkan/vulkan.hpp>

namespace utils
{
template <typename T>
struct is_vk_flags : std::false_type
{};

template <typename T>
struct is_vk_flags<vk::Flags<T>> : std::true_type
{};

template <typename T>
struct is_std_vector : std::false_type
{};

template <typename T, typename Allocator>
struct is_std_vector<std::vector<T, Allocator>> : std::true_type
{};

template <typename T>
struct magic_hash
{
	std::size_t operator()(const T & info) const noexcept
	{
		size_t h = 0;

		boost::pfr::for_each_field(info, [&](const auto & member) {
			using U = std::remove_cvref_t<decltype(member)>;

			if constexpr (is_vk_flags<U>::value)
			{
				using V = U::MaskType;
				std::hash<V> hasher;
				h = std::rotl(h, 5) ^ hasher(static_cast<V>(member));
			}
			else if constexpr (is_std_vector<U>::value)
			{
				std::hash<typename U::value_type> hasher;
				for (const auto & i: member)
					h = std::rotl(h, 5) ^ hasher(i);
			}
			else
			{
				std::hash<U> hasher;
				h = std::rotl(h, 5) ^ hasher(member);
			}
		});

		return h;
	}
};
} // namespace utils
