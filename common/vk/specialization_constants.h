/*
 * WiVRn VR streaming
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

#include <array>
#include <cstdint>
#include <tuple>
#include <vulkan/vulkan.hpp>

template <typename... Args>
class specialization_constants
{
	std::tuple<Args...> values;
	std::array<vk::SpecializationMapEntry, sizeof...(Args)> entries;
	vk::SpecializationInfo info_;

	specialization_constants(specialization_constants &&) = delete;
	specialization_constants(const specialization_constants &) = delete;
	specialization_constants & operator=(const specialization_constants &) = delete;
	specialization_constants & operator=(specialization_constants &&) = delete;

	template <uint32_t i = sizeof...(Args) - 1>
	void fill_entries()
	{
		entries[i] = vk::SpecializationMapEntry{
		        .constantID = i,
		        .offset = uint32_t(uintptr_t(std::addressof(std::get<i>(values))) - uintptr_t(std::addressof(values))),
		        .size = sizeof(std::get<i>(values)),
		};
		if constexpr (i > 0)
			fill_entries<i - 1>();
	}

public:
	specialization_constants(Args &&... args) :
	        values(std::forward<Args>(args)...),
	        info_{
	                .mapEntryCount = uint32_t(entries.size()),
	                .pMapEntries = entries.data(),
	                .dataSize = sizeof(values),
	                .pData = &values,
	        }
	{
		fill_entries();
	}

	operator vk::SpecializationInfo *()
	{
		return &info_;
	}

	vk::SpecializationInfo * info()
	{
		return &info_;
	}

	template <uint32_t i>
	auto & data()
	{
		return std::get<i>(values);
	}
};

template <typename... Args>
specialization_constants<Args...> make_specialization_constants(Args &&... args)
{
	return specialization_constants<Args...>(std::forward<Args>(args)...);
}
