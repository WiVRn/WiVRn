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

#include <cassert>
#include <concepts>

template<typename T>
class singleton
{
	// static_assert(std::derived_from<T, singleton<T>>);

	static T * instance_;

protected:
	singleton()
	{
		assert(instance_ == nullptr);
		instance_ = reinterpret_cast<T*>(this);
	}

	~singleton()
	{
		assert(instance_ == this);
		instance_ = nullptr;
	}

public:
	static T & instance()
	{
		assert(instance_ != nullptr);
		return *reinterpret_cast<T*>(instance_);
	}
};

template<typename T> T * singleton<T>::instance_;
