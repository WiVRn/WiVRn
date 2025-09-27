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

#pragma once

#include "utils/thread_safe.h"
#include <concepts>
#include <memory>
#include <unordered_map>

namespace utils
{
template <typename Key, typename Asset, typename Loader>
class cache
{
	Loader loader_;

	thread_safe<std::unordered_map<Key, std::shared_ptr<Asset>>> entries;

public:
	template <typename... Args>
	cache(Args &&... args) :
	        loader_(std::forward<Args>(args)...)
	{}

	template <typename... Args>
	        requires std::invocable<Loader, Args...>
	std::shared_ptr<Asset> load(const Key & key, Args &&... args)
	{
		auto _ = entries.lock();

		auto iter = _->find(key);
		if (iter != _->end())
			return iter->second;

		return _->emplace(key, loader_(std::forward<Args>(args)...)).first->second;
	}

	template <typename... Args>
	std::shared_ptr<Asset> load_uncached(Args &&... args)
	{
		return loader_(std::forward<Args>(args)...);
	}

	void clear()
	{
		auto _ = entries.lock();
		_->clear();
	}

	void remove(const Key & key)
	{
		auto _ = entries.lock();
		_->erase(key);
	}

	Loader & loader()
	{
		return loader_;
	}

	const Loader & loader() const
	{
		return loader_;
	}
};
} // namespace utils
