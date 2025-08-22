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

#include <memory>
#include <unordered_map>

namespace utils
{
template <typename Key, typename Asset, typename Loader>
class cache
{
	Loader loader;
	std::unordered_map<Key, std::shared_ptr<Asset>> entries;

public:
	template <typename... Args>
	cache(Args &&... args) :
	        loader(std::forward<Args>(args)...)
	{}

	template <typename... Args>
	std::shared_ptr<Asset> load(const Key & key, Args &&... args)
	{
		auto iter = entries.find(key);
		if (iter != entries.end())
			return iter->second;

		return entries.emplace(key, loader(std::forward<Args>(args)...)).first->second;
	}

	template <typename... Args>
	std::shared_ptr<Asset> load_uncached(Args &&... args)
	{
		return loader(std::forward<Args>(args)...);
	}
};
} // namespace utils
