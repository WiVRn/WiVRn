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

#include <spdlog/spdlog.h>
#include <type_traits>
#include <utility>
#include <variant>
#include <openxr/openxr.h>

namespace utils
{

template <typename T, auto Deleter = nullptr>
class handle
{
public:
	// true if the deleter is provided when constructing the object
	// typically if it's from an OpenXR extension
	static constexpr bool runtime_deleter = Deleter == nullptr;

protected:
	static const inline T null_value = 0;

	T id = null_value;
	[[no_unique_address]] std::conditional_t<runtime_deleter, XrResult (*)(T), std::monostate> deleter;

public:
	template <typename = void>
	        requires(not runtime_deleter)
	handle()
	{}

	template <typename = void>
	        requires(not runtime_deleter)
	handle(T id) :
	        id(id)
	{}

	template <typename = void>
	        requires(runtime_deleter)
	handle(XrResult (*deleter)(T)) :
	        deleter(deleter)
	{
		assert(deleter);
	}

	template <typename = void>
	        requires(runtime_deleter)
	handle(T id, XrResult (*deleter)(T)) :
	        id(id), deleter(deleter)
	{
		assert(deleter);
	}

	~handle()
	{
		if (id != null_value)
		{
			XrResult res = XR_SUCCESS;
			if constexpr (runtime_deleter)
				res = deleter(id);
			else
				res = Deleter(id);
			if (not XR_SUCCEEDED(res))
				spdlog::warn("Failed to destroy {}", typeid(T).name());
		}
	}

	handle(handle && other) noexcept :
	        id(other.id), deleter(other.deleter)
	{
		other.id = null_value;
	}

	handle & operator=(handle && other) noexcept
	{
		if constexpr (runtime_deleter)
			std::swap(deleter, other.deleter);
		std::swap(id, other.id);
		return *this;
	}

	handle(const handle &) = delete;
	handle & operator=(const handle &) = delete;

	operator T() const
	{
		return id;
	}

	operator bool() const
	{
		return id != null_value;
	}

	T release()
	{
		T r = id;
		id = null_value;
		return r;
	}
};

template <typename T, XrResult deleter(T)>
bool operator==(const handle<T, deleter> & lhs, const T & rhs)
{
	return (T)lhs == rhs;
}

template <typename T, XrResult deleter(T)>
bool operator==(const T & lhs, const handle<T, deleter> & rhs)
{
	return lhs == (T)rhs;
}

} // namespace utils
