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

#include "xr/actionset.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/swapchain.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// See http://www.nirfriedman.com/2018/04/29/unforgettable-factory/
// for automatic registration of scenes

class scene
{
public:
	struct action_binding
	{
		std::string action_name;
		std::string input_source;
	};

	struct suggested_binding
	{
		std::string profile_name;
		std::vector<action_binding> paths;
	};

	struct meta
	{
		// Filled by the scene class
		std::string name;
		std::vector<std::pair<std::string, XrActionType>> actions;
		std::vector<suggested_binding> bindings;

		// Filled by the application class
		xr::actionset actionset;
		std::unordered_map<std::string, std::pair<XrAction, XrActionType>> actions_by_name;
		std::unordered_map<std::string, xr::space> spaces_by_name;
	};

private:
	// Force derived classes to inherit from scene_impl<T> instead of scene
	class key
	{};
	template <typename T>
	friend class scene_impl;
	friend class application;

protected:
	static std::vector<meta *> scene_registry;

	xr::instance & instance;
	xr::system & system;
	xr::session & session;
	XrViewConfigurationType viewconfig;
	bool focused = false;

	vk::raii::Instance & vk_instance;
	vk::raii::Device & device;
	vk::raii::PhysicalDevice & physical_device;
	vk::raii::Queue & queue;
	vk::raii::CommandPool & commandpool;
	uint32_t queue_family_index;

	const meta & current_meta;

	std::pair<XrAction, XrActionType> get_action(const std::string & name)
	{
		return current_meta.actions_by_name.at(name);
	}

	XrSpace get_action_space(const std::string & name)
	{
		return current_meta.spaces_by_name.at(name);
	}

	virtual void on_unfocused();
	virtual void on_focused();

public:
	scene(key, const meta &);

	virtual ~scene();

	void set_focused(bool status);
	virtual void render(const XrFrameState &) = 0;
	virtual void on_xr_event(const xr::event &);
};

template <typename T>
class scene_impl : public scene
{
	friend T;
	friend scene;

	// Magically register all classes that derive from scene_impl
	static bool register_scene()
	{
		scene_registry.emplace_back(&T::get_meta_scene());
		return true;
	}

	static inline bool registered = scene_impl<T>::register_scene();

	scene_impl() :
	        scene(key{}, T::get_meta_scene())
	{
		(void)registered;
	}
};
