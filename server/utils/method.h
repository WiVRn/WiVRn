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

#include <functional>

namespace details
{
template <auto Method, typename = decltype(Method)>
struct method_trait
{};

template <auto Method, typename Result, typename Class, typename... Args>
struct method_trait<Method, Result (Class::*)(Args...)>
{
	static Result magic(Class::base * arg, Args... args)
	{
		return std::invoke(Method, static_cast<Class *>(arg), args...);
	}
	static Result magic2(Args... args, Class::base * arg)
	{
		return std::invoke(Method, static_cast<Class *>(arg), args...);
	}
};
} // namespace details

namespace wivrn
{

// Convert a pointer to member function to a free function that takes the object as first argument.
// The class needs to have a "base" type which will be used for the first argument.
//
// Example:
// class derived: public base_class
// {
// 	using base = base_class;
// 	void foo(int x);
// };
//
// method_pointer<&derived::foo> is equivalent to
// void free_foo(base_class * base, int x) {
// 	return static_cast<derived*>(base)->foo(x);
// }
template <auto Method>
auto method_pointer = ::details::method_trait<Method>::magic;

// same, but "this" argument is last
template <auto Method>
auto method_pointer2 = ::details::method_trait<Method>::magic2;
} // namespace wivrn
