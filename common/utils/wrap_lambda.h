/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <functional>

namespace details
{
template <typename... Args>
struct add_void;

template <typename T, typename... Args>
struct add_void<std::function<T(Args...)>>
{
	template <typename L>
	static T fn_0(void * userdata, Args... a)
	{
		L * f = (L *)userdata;
		return (*f)(a...);
	}

	template <typename L>
	static T fn(Args... a, void * userdata)
	{
		L * f = (L *)userdata;
		return (*f)(a...);
	}
};
} // namespace details

template <typename T>
class wrap_lambda
{
	T impl;

public:
	wrap_lambda(T && l) :
	        impl(l) {}

	auto userdata_first()
	{
		using F = decltype(std::function(impl));
		return details::add_void<F>::template fn_0<T>;
	}

	operator auto()
	{
		using F = decltype(std::function(impl));
		return details::add_void<F>::template fn<T>;
	}

	operator void *()
	{
		return this;
	}
};
